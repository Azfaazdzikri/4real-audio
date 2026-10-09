#include "FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fourreal
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr size_t kFftSize = 4096;

void fft (std::vector<cplx>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = 2.0 * kPi / static_cast<double> (len) * (inverse ? 1.0 : -1.0);
        const cplx wlen (std::cos (angle), std::sin (angle));
        for (size_t i = 0; i < n; i += len)
        {
            cplx w (1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const cplx u = a[i + j];
                const cplx v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto& x : a)
            x /= static_cast<double> (n);
}

// Linear interpolation of y(x), x ascending, edge values held outside the range.
double interp (double x, const std::vector<double>& xs, const std::vector<double>& ys)
{
    if (x <= xs.front()) return ys.front();
    if (x >= xs.back()) return ys.back();
    const auto it = std::upper_bound (xs.begin(), xs.end(), x);
    const size_t hi = static_cast<size_t> (it - xs.begin());
    const size_t lo = hi - 1;
    const double t = (x - xs[lo]) / (xs[hi] - xs[lo]);
    return ys[lo] + t * (ys[hi] - ys[lo]);
}

double toDb (const cplx& v)
{
    return 20.0 * std::log10 (std::max (std::abs (v), 1e-12));
}
} // namespace

std::vector<double> makeFrequencyGrid (double sampleRate, int points)
{
    const double fMin = 20.0;
    const double fMax = std::min (20000.0, sampleRate * 0.495);
    std::vector<double> f (static_cast<size_t> (points));
    for (int i = 0; i < points; ++i)
        f[static_cast<size_t> (i)] = fMin * std::pow (fMax / fMin, static_cast<double> (i) / (points - 1));
    return f;
}

std::vector<double> correctionDb (const std::vector<cplx>& source, const std::vector<cplx>& target,
                                  double maxBoostDb, double maxCutDb)
{
    std::vector<double> out (source.size());
    for (size_t i = 0; i < source.size(); ++i)
        out[i] = std::clamp (toDb (target[i]) - toDb (source[i]), maxCutDb, maxBoostDb);
    return out;
}

std::vector<float> designMinPhaseFir (const std::vector<double>& freqHz, const std::vector<double>& gainDb,
                                      double sampleRate, int numTaps)
{
    const size_t half = kFftSize / 2;
    std::vector<cplx> spec (kFftSize, cplx (0.0));

    for (size_t k = 0; k <= half; ++k)
    {
        const double f = static_cast<double> (k) * sampleRate / static_cast<double> (kFftSize);
        const double logMag = interp (f, freqHz, gainDb) / 20.0 * std::log (10.0);
        spec[k] = logMag;
        if (k > 0 && k < half)
            spec[kFftSize - k] = logMag;
    }

    fft (spec, true); // real cepstrum

    std::vector<cplx> folded (kFftSize, cplx (0.0));
    folded[0] = spec[0].real();
    for (size_t n = 1; n < half; ++n)
        folded[n] = 2.0 * spec[n].real();
    folded[half] = spec[half].real();

    fft (folded, false);
    for (auto& v : folded)
        v = std::exp (v);
    fft (folded, true);

    std::vector<float> taps (static_cast<size_t> (numTaps));
    const int fade = std::min(64, numTaps / 4);
    for (int i = 0; i < numTaps; ++i)
    {
        double w = 1.0;
        const int fromEnd = numTaps - 1 - i;
        if (fromEnd < fade)
            w = 0.5 * (1.0 - std::cos (kPi * static_cast<double> (fromEnd) / fade));
        taps[static_cast<size_t> (i)] = static_cast<float> (folded[static_cast<size_t> (i)].real() * w);
    }
    return taps;
}

std::vector<float> designVoicingTaps (const std::vector<cplx>& source, const std::vector<cplx>& target,
                                      const std::vector<double>& freqs, double sampleRate, bool keepLevel,
                                      int numTaps)
{
    std::vector<double> corr = correctionDb (source, target);
    if (! keepLevel)
    {
        std::vector<double> logF (freqs.size());
        for (size_t i = 0; i < freqs.size(); ++i)
            logF[i] = std::log (freqs[i]);
        const double ref = interp (std::log (1000.0), logF, corr);
        for (auto& c : corr)
            c -= ref;
    }
    return designMinPhaseFir (freqs, corr, sampleRate, numTaps);
}

std::vector<float> identityTaps (int numTaps)
{
    std::vector<float> taps (static_cast<size_t> (numTaps), 0.0f);
    taps[0] = 1.0f;
    return taps;
}

} // namespace fourreal
