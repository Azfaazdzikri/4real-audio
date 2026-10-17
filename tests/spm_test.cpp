// Checks the C++ spectral engine against golden vectors made by the numpy reference (training/nn/export_spec.py).
//   spm_test <model.spm> <golden_spm.json>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "MiniJson.h"
#include "SpecEngine.h"
#include "SpecModel.h"

using namespace fourreal;
using minijson::Json;

static std::string slurp (const char* path)
{
    std::ifstream f (path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } } while (0)

static std::vector<float> toFloat (const std::vector<double>& v) { return std::vector<float> (v.begin(), v.end()); }

// process in awkward chunk sizes to exercise the hop bookkeeping
static std::vector<float> run (SpecStream& s, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    const int chunks[] = { 137, 512, 1, 1000, 333, 2048, 77 };
    size_t pos = 0;
    int k = 0;
    while (pos < in.size())
    {
        const size_t n = std::min (static_cast<size_t> (chunks[k++ % 7]), in.size() - pos);
        s.process (in.data() + pos, out.data() + pos, static_cast<int> (n));
        pos += n;
    }
    return out;
}

static void compare (const char* what, const std::vector<float>& got, const std::vector<float>& want)
{
    double maxErr = 0.0, errPow = 0.0, refPow = 0.0;
    for (size_t i = 0; i < want.size(); ++i)
    {
        const double d = static_cast<double> (got[i]) - want[i];
        maxErr = std::max (maxErr, std::abs (d));
        errPow += d * d;
        refPow += static_cast<double> (want[i]) * want[i];
    }
    const double snr = 10.0 * std::log10 (refPow / std::max (errPow, 1e-30));
    std::printf ("  %-28s max abs error %.2e, error %.1f dB below the signal\n", what, maxErr, -snr);
    CHECK (snr > 80.0, "%s differs from the reference (only %.1f dB below)", what, snr);
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: spm_test model.spm golden_spm.json\n"); return 2; }

    const std::string raw = slurp (argv[1]);
    SpecModel m;
    std::string err;
    if (! SpecModel::load (raw.data(), raw.size(), m, err)) { std::printf ("load failed: %s\n", err.c_str()); return 1; }
    std::printf ("%s: %d targets x %d sources, %d ch, %d bands, %zu blocks\n", m.name.c_str(), m.numTargets, m.numSources,
                 m.channels, m.bands, m.blocks.size());
    CHECK (m.findSource (m.targetCode, 0) >= 0, "own guitar position 0 is a source");

    const std::string gtxt = slurp (argv[2]);
    const Json golden = minijson::Parser (gtxt.c_str(), gtxt.size()).parse();
    const auto input = toFloat (golden.at ("input").nums);

    SpecStream s;
    s.prepare (m, 48000.0);

    int caseNo = 0;
    for (const auto& c : golden.at ("cases").items)
    {
        const int source = static_cast<int> (c.at ("source").number), target = static_cast<int> (c.at ("target").number);
        std::printf ("case %d: source %d -> target %d\n", caseNo, source, target);

        s.reset();
        s.setCondition (target, source);
        s.setIntensity (1.0f);
        s.setAnalysisGain (1.0f);
        compare ("filtered audio", run (s, input), toFloat (c.at ("out").nums));

        // gain curves of selected hops (hop-sized calls so hopGains() is read right after that hop started)
        s.reset();
        s.setCondition (target, source);
        const auto& hops = c.at ("gain_hops").nums;
        const auto& gains = c.at ("gains").items;
        std::vector<float> scratch (static_cast<size_t> (m.hop));
        size_t next = 0;
        for (int j = 0; next < hops.size(); ++j)
        {
            s.process (input.data() + static_cast<size_t> (j) * static_cast<size_t> (m.hop), scratch.data(), m.hop);
            if (j == static_cast<int> (hops[next]))
            {
                double maxDb = 0.0;
                for (int b = 0; b < m.bands; ++b)
                    maxDb = std::max (maxDb, std::abs (s.hopGains()[static_cast<size_t> (b)] - gains[next].nums[static_cast<size_t> (b)]));
                std::printf ("  hop %3d gain curve: max difference %.4f dB\n", j, maxDb);
                CHECK (maxDb < 0.02, "hop %d gain curve differs by %.4f dB", j, maxDb);
                ++next;
            }
        }

        if (const Json* x15 = c.find ("out_x1_5"))
        {
            s.reset();
            s.setCondition (target, source);
            s.setIntensity (1.5f);
            compare ("filtered audio, intensity 1.5", run (s, input), toFloat (x15->nums));
        }

        // reset gives the same result again (no stale state)
        s.reset();
        s.setCondition (target, source);
        s.setIntensity (1.0f);
        const auto again = run (s, input);
        s.reset();
        s.setCondition (target, source);
        const auto once = run (s, input);
        CHECK (again == once, "output after reset() is not deterministic");
        ++caseNo;
    }

    // other host rates must still run and stay finite
    for (double rate : { 44100.0, 96000.0 })
    {
        SpecStream other;
        other.prepare (m, rate);
        other.setCondition (0, 0);
        const auto y = run (other, input);
        bool finite = true;
        for (float v : y)
            finite = finite && std::isfinite (v);
        CHECK (finite, "non-finite output at %.0f Hz", rate);
    }

    // cost: 10 s of audio, one channel
    {
        std::vector<float> noise (480000);
        unsigned r = 1;
        for (auto& v : noise) { r = r * 1664525u + 1013904223u; v = (static_cast<float> (r >> 8) / 8388608.0f - 1.0f) * 0.05f; }
        s.reset();
        s.setCondition (0, 0);
        std::vector<float> out (noise.size());
        const auto t0 = std::chrono::steady_clock::now();
        s.process (noise.data(), out.data(), static_cast<int> (noise.size()));
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("cost: %.2f s for 10 s of audio = %.1f %% of one core per channel\n", sec, sec / 10.0 * 100.0);
    }

    std::printf (failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
