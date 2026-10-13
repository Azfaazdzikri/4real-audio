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
#include "GuitarModels.h"

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

    // --- Generic model registry ---------------------------------------------------------------
    {
        const auto& models = allModels();
        check (models.size() == 8, "registry has the 8 recorded guitars");

        // Selector positions must match what was recorded in the training data (17 cells in total).
        const std::vector<std::pair<std::string, size_t>> recorded { { "LP", 3 }, { "335", 1 }, { "SG", 2 }, { "PRS", 3 },
                                                                      { "RIC", 1 }, { "STR", 4 }, { "TCR", 2 }, { "FLC", 1 } };
        size_t cells = 0;
        for (const auto& [code, count] : recorded)
        {
            const auto* m = findModelByCode (code);
            check (m != nullptr && m->positions.size() == count, "recorded positions for " + code);
            cells += m != nullptr ? m->positions.size() : 0;
        }
        check (cells == 17, "17 recorded guitar + pickup-position cells");
        check (std::string (models.front().code) == "LP", "default source guitar is the Les Paul");

        const std::vector<std::string> codes { "LP", "335", "SG", "PRS", "RIC", "STR", "TCR", "FLC" };
        for (const auto& code : codes)
            check (findModelByCode (code) != nullptr, "model code exists: " + code);

        // The generic solver must reproduce the legacy Strat / Les Paul functions.
        auto maxDiffDb = [&] (const std::vector<cplx>& a, const std::vector<cplx>& b)
        {
            double worst = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
                worst = std::max (worst, std::abs (db (a[i]) - db (b[i])));
            return worst;
        };
        const auto* strat = findModelByCode ("STR");
        const auto* lp = findModelByCode ("LP");
        check (strat && lp, "STR and LP models found");

        double worstStrat = 0.0, worstLp = 0.0;
        for (int i = 0; i < (int) strat->positions.size(); ++i)
        {
            const int legacyPos = strat->positions[(size_t) i].name[0] - '0'; // names start with the real 1..5 number
            const double vol = i % 2 ? 6.0 : 10.0, tn = i == 2 ? 2.0 : 10.0, tm = i == 1 ? 3.0 : 10.0;
            ModelSettings st; st.position = i; st.volumes = { vol }; st.tones = { tn, tm };
            worstStrat = std::max (worstStrat, maxDiffDb (modelResponse (testFreqs, *strat, st),
                                   stratResponse (testFreqs, StratKind::SSS, { legacyPos, vol, tn, tm })));
        }
        const LesPaulSelector legacySel[3] = { LesPaulSelector::Bridge, LesPaulSelector::Both, LesPaulSelector::Neck };
        for (int pos = 0; pos < 3; ++pos)
        {
            ModelSettings st; st.position = pos; st.volumes = { 6.0, 9.0 }; st.tones = { 2.0, 7.0 }; // neck, bridge
            worstLp = std::max (worstLp, maxDiffDb (modelResponse (testFreqs, *lp, st),
                                lesPaulResponse (testFreqs, { legacySel[pos], 6.0, 9.0, 2.0, 7.0 })));
        }
        std::printf("generic vs legacy: STR %.4f dB, LP %.4f dB\n", worstStrat, worstLp);
        check (worstStrat < 0.05 && worstLp < 0.05,
               "generic model solver matches the legacy Strat and Les Paul circuits (<0.05 dB)");

        // Sanity checks on every model and every selector position.
        for (const auto& m : models)
        {
            bool maskOk = ! m.positions.empty() && ! m.pickups.empty();
            for (const auto& p : m.positions)
                maskOk = maskOk && p.pickupMask != 0 && p.pickupMask < (1u << m.pickups.size());
            check (maskOk, std::string ("selector masks are valid: ") + m.id);

            for (size_t pos = 0; pos < m.positions.size(); ++pos)
            {
                ModelSettings full; full.position = (int) pos;
                const auto base = modelResponse (testFreqs, m, full);
                bool finite = true;
                for (auto v : base) finite = finite && std::isfinite (db (v));

                // Volume at 0 silences the output; volume at 5 is quieter than 10 at 1 kHz.
                bool volOk = true;
                for (size_t v = 0; v < m.volumes.size(); ++v)
                {
                    const bool active = m.volumes[v].pickup < 0
                                        || (m.positions[pos].pickupMask & (1u << m.volumes[v].pickup)) != 0;
                    if (! active) continue;
                    ModelSettings low = full; low.volumes.assign (m.volumes.size(), 10.0); low.volumes[v] = 0.0;
                    const auto r = modelResponse (testFreqs, m, low);
                    // With two independent pickups the other channel may still be audible, so only
                    // require a clear drop for single-channel and master volumes.
                    const bool single = m.volumes[v].pickup < 0 || (m.positions[pos].pickupMask & (m.positions[pos].pickupMask - 1)) == 0;
                    if (single) volOk = volOk && db (r[4]) < db (base[4]) - 40.0;
                    else        volOk = volOk && db (r[4]) < db (base[4]) + 0.01;
                }

                // Tone at 0 darkens the top end (4 kHz drops relative to 100 Hz) for tones in circuit.
                bool toneOk = true;
                for (size_t t = 0; t < m.tones.size(); ++t)
                {
                    const bool active = m.tones[t].pickup >= 0
                        ? (m.positions[pos].pickupMask & (1u << m.tones[t].pickup)) != 0
                        : (m.tones[t].activeMask & m.positions[pos].pickupMask) != 0;
                    if (! active) continue;
                    ModelSettings dark = full; dark.tones.assign (m.tones.size(), 10.0); dark.tones[t] = 0.0;
                    const auto r = modelResponse (testFreqs, m, dark);
                    const double baseTilt = db (base[7]) - db (base[2]);
                    const double darkTilt = db (r[7]) - db (r[2]);
                    toneOk = toneOk && darkTilt < baseTilt - 0.3;
                }
                check (finite && volOk && toneOk,
                       std::string ("finite, volume and tone behave: ") + m.id + " pos " + std::to_string (pos));
            }
        }
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
