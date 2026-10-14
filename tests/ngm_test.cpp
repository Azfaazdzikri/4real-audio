// Checks the C++ .ngm engine against golden vectors produced with PyTorch (training/nn/export_ngm.py).
//   ngm_test <model.ngm> <golden.json>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "NgmEngine.h"
#include "NgmModel.h"
#include "CrossfadeFir.h"
#include "FilterDesign.h"

using namespace fourreal;

static std::string slurp (const char* path)
{
    std::ifstream f (path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// tiny extraction of  "key":[numbers]  from the golden file (it has a fixed, known layout)
static std::vector<float> arrayAfter (const std::string& s, size_t& pos, const char* key)
{
    pos = s.find (std::string ("\"") + key + "\":[", pos);
    if (pos == std::string::npos) { std::fprintf (stderr, "golden: missing %s\n", key); std::exit (2); }
    pos = s.find ('[', pos) + 1;
    std::vector<float> v;
    const char* p = s.c_str() + pos;
    char* e = nullptr;
    for (;;)
    {
        v.push_back (std::strtof (p, &e));
        p = e;
        if (*p == ',') { ++p; continue; }
        break;
    }
    pos = static_cast<size_t> (p - s.c_str());
    return v;
}

static double rms (const std::vector<float>& a, const std::vector<float>& b)
{
    double s = 0;
    for (size_t i = 0; i < a.size(); ++i) { const double d = a[i] - (b.empty() ? 0.0 : b[i]); s += d * d; }
    return std::sqrt (s / static_cast<double> (a.size()));
}

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("  FAIL: %s\n", msg); ++failures; } } while (0)

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: ngm_test model.ngm golden.json\n"); return 2; }

    const std::string raw = slurp (argv[1]);
    NgmModel m;
    std::string err;
    if (! NgmModel::load (raw.data(), raw.size(), m, err)) { std::printf ("load failed: %s\n", err.c_str()); return 1; }
    std::printf ("%s: %d targets x %d sources, %d ch, %d blocks, receptive field %d samples\n", m.name.c_str(), m.numTargets,
                 m.numSources, m.channels, m.numBlocks, m.receptiveField());
    CHECK (m.findSource (m.targetCode, 0) >= 0, "own guitar position 0 is a source");

    const std::string g = slurp (argv[2]);
    size_t pos = 0;
    const auto input = arrayAfter (g, pos, "input");
    (void) input;
    struct Case { int source, target; std::vector<float> eq, out; };
    std::vector<Case> cases;
    for (int c = 0; c < 3; ++c)
    {
        Case k;
        pos = g.find ("\"source\":", pos);
        k.source = std::atoi (g.c_str() + pos + 9);
        pos = g.find ("\"target\":", pos);
        k.target = std::atoi (g.c_str() + pos + 9);
        k.eq = arrayAfter (g, pos, "eq_out");
        k.out = arrayAfter (g, pos, "out");
        cases.push_back (std::move (k));
    }

    for (size_t ci = 0; ci < cases.size(); ++ci)
    {
        const auto& k = cases[ci];
        std::printf ("case %zu: source %d -> target %d\n", ci, k.source, k.target);

        // 1) engine exactness: feed the golden post-EQ signal, compare with the full torch output
        std::vector<float> res (k.eq.size());
        for (size_t i = 0; i < res.size(); ++i) res[i] = k.out[i] - k.eq[i]; // torch residual
        NgmStream s;
        s.prepare (m);
        s.setCondition (k.target, k.source, true);
        std::vector<float> y (k.eq.size());
        s.process (k.eq.data(), y.data(), static_cast<int> (y.size()));
        const double errRms = rms (y, k.out);
        const double resRms = rms (res, {});
        std::printf ("  engine vs torch: error rms %.3e, torch residual rms %.3e (error = %.1f dB below the residual)\n", errRms,
                     resRms, 20.0 * std::log10 (resRms / std::max (errRms, 1e-30)));
        CHECK (errRms < 0.02 * resRms, "engine output must match torch (error < 2% of the residual)");

        // 2) chunk-size invariance
        for (int chunk : { 1, 37, 511, 513, 4800 })
        {
            NgmStream s2;
            s2.prepare (m);
            s2.setCondition (k.target, k.source, true);
            std::vector<float> y2 (k.eq.size());
            for (int i = 0; i < (int) y2.size(); i += chunk)
                s2.process (k.eq.data() + i, y2.data() + i, std::min (chunk, (int) y2.size() - i));
            double maxd = 0;
            for (size_t i = 0; i < y.size(); ++i) maxd = std::max (maxd, (double) std::abs (y[i] - y2[i]));
            if (maxd > 1e-6) std::printf ("  chunk %d: max diff %.3e\n", chunk, maxd);
            CHECK (maxd < 1e-6, "result must not depend on how the host splits blocks");
        }

        // 3) whole pipeline with the min-phase EQ FIR (what the plugin does) vs torch (zero-phase EQ)
        CrossfadeFir fir;
        fir.prepare (1, kVoicingTaps, 4800, 1);
        const auto taps = designEqTaps (m, k.target, k.source, 48000.0);
        fir.setTaps (taps.data());
        std::vector<float> warm (4800, 0.0f), x (input);
        float* pw[1] = { warm.data() };
        fir.process (pw, 1, 4800); // finish the (1-sample) crossfade
        float* px[1] = { x.data() };
        fir.process (px, 1, 4800);
        NgmStream s3;
        s3.prepare (m);
        s3.setCondition (k.target, k.source, true);
        std::vector<float> y3 (x.size());
        s3.process (x.data(), y3.data(), (int) x.size());
        std::printf ("  full pipeline (min-phase EQ) vs torch: error %.1f dB below the output (rms %.4f)\n",
                     20.0 * std::log10 (rms (k.out, {}) / rms (y3, k.out)), rms (k.out, {}));
        for (float v : y3) CHECK (std::isfinite (v), "finite output");
    }

    // 4) switching the condition while running stays finite and bounded
    {
        NgmStream s;
        s.prepare (m);
        s.setCondition (0, 0, true);
        std::vector<float> x (48000), y (48000);
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.03f * std::sin (0.05f * (float) i);
        for (int i = 0; i < 48000; i += 480)
        {
            if (i % 9600 == 0) s.setCondition ((i / 9600) % m.numTargets, (i / 9600) % m.numSources, false);
            s.process (x.data() + i, y.data() + i, 480);
        }
        double peak = 0;
        for (float v : y) { CHECK (std::isfinite (v), "finite while switching"); peak = std::max (peak, (double) std::abs (v)); }
        CHECK (peak < 0.2, "bounded while switching");
        std::printf ("switching test: peak %.4f\n", peak);
    }

    // 5) speed
    {
        NgmStream s;
        s.prepare (m);
        s.setCondition (0, 0, true);
        const int N = 48000 * 10;
        std::vector<float> x (N), y (N);
        for (int i = 0; i < N; ++i) x[i] = 0.03f * std::sin (0.05f * (float) i);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; i += 512) s.process (x.data() + i, y.data() + i, std::min (512, N - i));
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("speed: 10 s of audio in %.3f s -> %.1f%% of one core per channel\n", sec, sec * 10.0);
    }

    std::printf (failures ? "FAILED (%d)\n" : "ngm_test: all checks passed\n", failures);
    return failures ? 1 : 0;
}
