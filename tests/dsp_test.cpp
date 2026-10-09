// Standalone tests for Source/dsp (no JUCE needed).
//   g++ -std=c++17 -O2 -ISource/dsp Source/dsp/GuitarCircuit.cpp Source/dsp/FilterDesign.cpp tests/dsp_test.cpp -o dsp_test
//   ./dsp_test
// Prints "RESP <case> <freq> <dB>" lines (compared with the Python model by
// tests/check_parity.py) and returns non-zero if any self-check fails.
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "CrossfadeFir.h"
#include "FilterDesign.h"
#include "GuitarCircuit.h"

using namespace fourreal;

static int failures = 0;
static constexpr double kPi = 3.14159265358979323846;

static void check (bool ok, const std::string& what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

static double db (cplx v) { return 20.0 * std::log10 (std::abs (v)); }

static double firMagnitudeDb (const std::vector<float>& h, double f, double fs)
{
    cplx acc (0.0);
    for (size_t n = 0; n < h.size(); ++n)
        acc += static_cast<double> (h[n]) * std::polar (1.0, -2.0 * kPi * f * static_cast<double> (n) / fs);
    return db (acc);
}

int main()
{
    const std::vector<double> testFreqs { 20, 50, 100, 300, 1000, 2000, 3000, 4000, 6000, 10000, 20000 };

    struct Case { std::string name; std::function<std::vector<cplx>()> run; };
    Rig cable200; cable200.cablePf = 200.0;
    const std::vector<Case> cases {
        { "hss_pos1", [&] { return stratResponse (testFreqs, StratKind::HSS, { 1, 10, 10, 10 }); } },
        { "hss_pos2_v7_tm3", [&] { return stratResponse (testFreqs, StratKind::HSS, { 2, 7, 10, 3 }); } },
        { "sss_pos5_tn0", [&] { return stratResponse (testFreqs, StratKind::SSS, { 5, 10, 0, 10 }); } },
        { "sss_pos4_v5_tn4_tm8", [&] { return stratResponse (testFreqs, StratKind::SSS, { 4, 5, 4, 8 }); } },
        { "lp_bridge", [&] { return lesPaulResponse (testFreqs, {}); } },
        { "lp_both_mixed", [&] { return lesPaulResponse (testFreqs, { LesPaulSelector::Both, 6, 9, 2, 7 }); } },
        { "lp_neck_tn0", [&] { return lesPaulResponse (testFreqs, { LesPaulSelector::Neck, 10, 10, 0, 10 }); } },
        { "lp_bridge_cable200", [&] { return lesPaulResponse (testFreqs, {}, cable200); } },
    };
    for (const auto& c : cases)
    {
        const auto r = c.run();
        for (size_t i = 0; i < testFreqs.size(); ++i)
            std::printf("RESP %s %.1f %.6f\n", c.name.c_str(), testFreqs[i], db (r[i]));
    }

    // --- FIR design: Strat SSS neck -> Les Paul bridge at 48 kHz -------------------
    const double fs = 48000.0;
    const auto grid = makeFrequencyGrid (fs);
    const auto src = stratResponse (grid, StratKind::SSS, { 5, 10, 10, 10 });
    const auto tgt = lesPaulResponse (grid, {});
    const auto corr = correctionDb (src, tgt);
    const auto taps = designVoicingTaps (src, tgt, grid, fs, true);

    double maxErr = 0.0;
    for (double f = 100.0; f <= 12000.0; f *= 1.07)
    {
        size_t i = 0;
        while (i + 1 < grid.size() && grid[i + 1] < f) ++i;
        const double t = (f - grid[i]) / (grid[i + 1] - grid[i]);
        const double want = corr[i] + t * (corr[i + 1] - corr[i]);
        maxErr = std::max (maxErr, std::abs (firMagnitudeDb (taps, f, fs) - want));
    }
    std::printf("FIR max error vs target: %.3f dB\n", maxErr);
    check (maxErr < 1.0, "designed FIR matches target magnitude within 1 dB (100 Hz-12 kHz)");

    double early = 0.0, total = 0.0;
    for (size_t n = 0; n < taps.size(); ++n)
    {
        total += static_cast<double> (taps[n]) * taps[n];
        if (n < 64) early += static_cast<double> (taps[n]) * taps[n];
    }
    check (early > 0.95 * total, "FIR is minimum phase (energy concentrated at the start)");

    const auto noLevel = designVoicingTaps (src, tgt, grid, fs, false);
    check (std::abs (firMagnitudeDb (noLevel, 1000.0, fs)) < 0.5, "keepLevel=false gives ~0 dB at 1 kHz");

    const auto ident = identityTaps();
    check (std::abs (firMagnitudeDb (ident, 3000.0, fs)) < 1e-6, "identity taps are flat");

    // --- CrossfadeFir -------------------------------------------------------------
    std::mt19937 rng (7);
    std::normal_distribution<float> dist (0.0f, 0.3f);
    const int length = 20000;
    std::vector<float> input (static_cast<size_t> (length));
    for (auto& v : input) v = dist (rng);

    auto runBlocks = [&] (CrossfadeFir& fir, std::vector<float>& buf, int block, int swapAt, const std::vector<float>& newTaps)
    {
        for (int pos = 0; pos < length; pos += block)
        {
            if (swapAt >= 0 && pos >= swapAt && pos < swapAt + block)
                fir.setTaps (newTaps.data());
            float* ch[1] = { buf.data() + pos };
            fir.process (ch, 1, std::min (block, length - pos));
        }
    };

    {
        CrossfadeFir fir;
        fir.prepare (1, kVoicingTaps, 256, 2048);
        std::vector<float> buf = input;
        runBlocks (fir, buf, 37, -1, {});
        double maxDiff = 0.0;
        for (size_t i = 0; i < buf.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (buf[i] - input[i]));
        check (maxDiff < 1e-6, "FIR starts as an exact passthrough");
    }
    {
        CrossfadeFir fir;
        fir.prepare (1, kVoicingTaps, 256, 2048);
        std::vector<float> buf = input;
        runBlocks (fir, buf, 37, 1000, taps); // odd block size, swap mid-stream
        // after the 2048-sample fade the output must equal direct convolution with taps
        double maxDiff = 0.0;
        for (int n = 1000 + 2048 + kVoicingTaps; n < length; ++n)
        {
            double ref = 0.0;
            for (int k = 0; k < kVoicingTaps; ++k) ref += static_cast<double> (taps[(size_t) k]) * input[(size_t) (n - k)];
            maxDiff = std::max (maxDiff, std::abs (ref - buf[(size_t) n]));
        }
        std::printf("convolution max diff after fade: %.2e\n", maxDiff);
        check (maxDiff < 1e-4, "crossfade FIR converges to direct convolution (block-size independent)");

        bool finite = true;
        for (float v : buf) finite = finite && std::isfinite (v);
        check (finite, "output is finite");
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
