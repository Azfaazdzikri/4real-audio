#include "SpecEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define FOURREAL_SSE2 1
#endif

namespace fourreal
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

float dotProduct (const float* a, const float* b, int n)
{
#ifdef FOURREAL_SSE2
    __m128 s0 = _mm_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
    int i = 0;
    for (; i + 16 <= n; i += 16)
    {
        s0 = _mm_add_ps (s0, _mm_mul_ps (_mm_loadu_ps (a + i), _mm_loadu_ps (b + i)));
        s1 = _mm_add_ps (s1, _mm_mul_ps (_mm_loadu_ps (a + i + 4), _mm_loadu_ps (b + i + 4)));
        s2 = _mm_add_ps (s2, _mm_mul_ps (_mm_loadu_ps (a + i + 8), _mm_loadu_ps (b + i + 8)));
        s3 = _mm_add_ps (s3, _mm_mul_ps (_mm_loadu_ps (a + i + 12), _mm_loadu_ps (b + i + 12)));
    }
    s0 = _mm_add_ps (_mm_add_ps (s0, s1), _mm_add_ps (s2, s3));
    alignas (16) float t[4];
    _mm_store_ps (t, s0);
    float sum = (t[0] + t[1]) + (t[2] + t[3]);
    for (; i < n; ++i)
        sum += a[i] * b[i];
    return sum;
#else
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    float sum = (s0 + s1) + (s2 + s3);
    for (; i < n; ++i)
        sum += a[i] * b[i];
    return sum;
#endif
}

inline float sigmoidf (float x) { return 1.0f / (1.0f + std::exp (-x)); }
inline float siluf (float x) { return x * sigmoidf (x); }
} // namespace

// ----------------------------------------------------------------------------------------------- FFT

void Fft::prepare (int n)
{
    n_ = n;
    rev_.assign (static_cast<size_t> (n), 0);
    int bits = 0;
    while ((1 << bits) < n)
        ++bits;
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        rev_[static_cast<size_t> (i)] = r;
    }
    cos_.resize (static_cast<size_t> (n / 2));
    sin_.resize (static_cast<size_t> (n / 2));
    for (int i = 0; i < n / 2; ++i)
    {
        cos_[static_cast<size_t> (i)] = std::cos (2.0 * kPi * i / n);
        sin_[static_cast<size_t> (i)] = std::sin (2.0 * kPi * i / n);
    }
}

void Fft::run (double* re, double* im, bool inverse) const
{
    const int n = n_;
    for (int i = 0; i < n; ++i)
    {
        const int j = rev_[static_cast<size_t> (i)];
        if (j > i)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    const double sgn = inverse ? 1.0 : -1.0;
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2, step = n / len;
        for (int i = 0; i < n; i += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const double wr = cos_[static_cast<size_t> (k * step)];
                const double wi = sgn * sin_[static_cast<size_t> (k * step)];
                const int a = i + k, b = a + half;
                const double tr = re[b] * wr - im[b] * wi;
                const double ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
    if (inverse)
    {
        const double s = 1.0 / n;
        for (int i = 0; i < n; ++i)
        {
            re[i] *= s;
            im[i] *= s;
        }
    }
}

// ----------------------------------------------------------------------------------------------- stream

void SpecStream::prepare (const SpecModel& m, double sampleRate)
{
    model_ = &m;
    sampleRate_ = sampleRate;
    bands_ = m.bands;
    ch_ = m.channels;
    nBlocks_ = static_cast<int> (m.blocks.size());
    framesPerWindow_ = std::max (1, m.nfft / m.hop);

    // Everything is defined in time, not in samples, so a 44.1 kHz or 96 kHz project behaves like the 48 kHz the model was
    // trained at: same 10.7 ms hop, same 85 ms analysis window (a whole number of hops, so a frame is complete when a hop
    // starts), same 21 ms filter length. The bands are defined in Hz and follow the sample rate automatically.
    const double scale = sampleRate / m.sampleRate;
    hop_ = std::max (16, static_cast<int> (std::lround (m.hop * scale)));
    winLen_ = framesPerWindow_ * hop_;
    nfft_ = 1;
    while (nfft_ < winLen_)
        nfft_ <<= 1;
    taps_ = std::clamp (static_cast<int> (std::lround (m.taps * scale)), 64, 2048);
    dfft_ = 1;
    while (dfft_ < taps_ * (m.designFft / m.taps))
        dfft_ <<= 1;

    fftA_.prepare (nfft_);
    fftD_.prepare (dfft_);

    // periodic Hann window over the analysis window (same as np.hanning(n + 1)[:-1]), zero padded up to the FFT size
    window_.resize (static_cast<size_t> (winLen_));
    windowNorm_ = 0.0;
    for (int n = 0; n < winLen_; ++n)
    {
        window_[static_cast<size_t> (n)] = 0.5 - 0.5 * std::cos (2.0 * kPi * n / winLen_);
        windowNorm_ += window_[static_cast<size_t> (n)] * window_[static_cast<size_t> (n)];
    }

    // band -> bins (inclusive ranges); a band narrower than one bin uses the nearest bin
    const int nBins = nfft_ / 2 + 1;
    bandLo_.assign (static_cast<size_t> (bands_), 0);
    bandHi_.assign (static_cast<size_t> (bands_), 0);
    std::vector<double> centers (static_cast<size_t> (bands_));
    for (int i = 0; i < bands_; ++i)
    {
        const double lo = m.bandEdgesHz[static_cast<size_t> (i)], hi = m.bandEdgesHz[static_cast<size_t> (i) + 1];
        centers[static_cast<size_t> (i)] = std::sqrt (lo * hi);
        int first = -1, last = -1;
        for (int k = 0; k < nBins; ++k)
        {
            const double f = k * sampleRate_ / nfft_;
            if (f >= lo && f < hi)
            {
                if (first < 0)
                    first = k;
                last = k;
            }
        }
        if (first < 0)
        {
            double best = 1e30;
            for (int k = 0; k < nBins; ++k)
            {
                const double d = std::abs (k * sampleRate_ / nfft_ - centers[static_cast<size_t> (i)]);
                if (d < best)
                {
                    best = d;
                    first = last = k;
                }
            }
        }
        bandLo_[static_cast<size_t> (i)] = first;
        bandHi_[static_cast<size_t> (i)] = last;
    }

    rangeMask_.assign (static_cast<size_t> (bands_), 0.0);
    rangeCount_ = 0.0;
    for (int i = 0; i < bands_; ++i)
        if (centers[static_cast<size_t> (i)] >= m.rangeLoHz && centers[static_cast<size_t> (i)] <= m.rangeHiHz)
        {
            rangeMask_[static_cast<size_t> (i)] = 1.0;
            rangeCount_ += 1.0;
        }
    if (rangeCount_ < 1.0)
        rangeCount_ = 1.0;

    // design FFT bin -> interpolation between band centres (linear in Hz, edge values held), like np.interp
    const int dBins = dfft_ / 2 + 1;
    designIdx_.assign (static_cast<size_t> (dBins), 0);
    designFrac_.assign (static_cast<size_t> (dBins), 0.0);
    for (int k = 0; k < dBins; ++k)
    {
        const double f = k * sampleRate_ / dfft_;
        if (f <= centers.front())
        {
            designIdx_[static_cast<size_t> (k)] = 0;
            designFrac_[static_cast<size_t> (k)] = 0.0;
        }
        else if (f >= centers.back())
        {
            designIdx_[static_cast<size_t> (k)] = bands_ - 2;
            designFrac_[static_cast<size_t> (k)] = 1.0;
        }
        else
        {
            int i0 = 0;
            while (i0 + 1 < bands_ - 1 && centers[static_cast<size_t> (i0) + 1] <= f)
                ++i0;
            designIdx_[static_cast<size_t> (k)] = i0;
            designFrac_[static_cast<size_t> (k)] = (f - centers[static_cast<size_t> (i0)])
                                                    / (centers[static_cast<size_t> (i0) + 1] - centers[static_cast<size_t> (i0)]);
        }
    }

    // control points -> bands (linear in the band index, as in training)
    ctrlIdx_.assign (static_cast<size_t> (bands_), 0);
    ctrlFrac_.assign (static_cast<size_t> (bands_), 0.0);
    for (int j = 0; j < bands_; ++j)
    {
        const double p = j * static_cast<double> (m.nCtrl - 1) / static_cast<double> (bands_ - 1);
        const int i0 = std::min (static_cast<int> (std::floor (p)), m.nCtrl - 2);
        ctrlIdx_[static_cast<size_t> (j)] = i0;
        ctrlFrac_[static_cast<size_t> (j)] = p - i0;
    }

    ring_.assign (static_cast<size_t> (nfft_), 0.0f);
    re_.assign (static_cast<size_t> (std::max (nfft_, dfft_)), 0.0);
    im_.assign (static_cast<size_t> (std::max (nfft_, dfft_)), 0.0);
    bandDb_.assign (static_cast<size_t> (bands_), 0.0);

    feat_.assign (static_cast<size_t> (bands_ + 1), 0.0f);
    h_.assign (static_cast<size_t> (ch_), 0.0f);
    z_.assign (static_cast<size_t> (2 * ch_), 0.0f);
    skip_.assign (static_cast<size_t> (ch_), 0.0f);
    tmp_.assign (static_cast<size_t> (ch_), 0.0f);
    ctrl_.assign (static_cast<size_t> (m.nCtrl), 0.0f);
    hist_.clear();
    for (const auto& b : m.blocks)
        hist_.emplace_back (static_cast<size_t> ((2 * b.dilation + 1) * ch_), 0.0f);
    histPos_.assign (static_cast<size_t> (nBlocks_), 0);
    delta_.assign (static_cast<size_t> (bands_), 0.0);
    gains_.assign (static_cast<size_t> (bands_), 0.0);

    curRev_.assign (static_cast<size_t> (taps_), 0.0f);
    prevRev_.assign (static_cast<size_t> (taps_), 0.0f);
    buf_.assign (static_cast<size_t> (taps_ - 1 + 8192), 0.0f);
    ramp_.resize (static_cast<size_t> (hop_));
    for (int k = 0; k < hop_; ++k)
        ramp_[static_cast<size_t> (k)] = static_cast<float> (k + 1) / static_cast<float> (hop_);

    // automatic source (allocated here so the audio thread never allocates)
    const SpmAuto& au = m.autoSource;
    autoW_.assign (static_cast<size_t> (m.numSources), 1.0 / m.numSources);
    autoAcc_.assign (static_cast<size_t> (bands_), 0.0);
    autoFeat_.assign (static_cast<size_t> (std::max (1, au.nMask)), 0.0);
    autoMix_.assign (static_cast<size_t> (bands_), 0.0);
    filmBuf_.assign (static_cast<size_t> (nBlocks_ * 4 * ch_), 0.0f);
    condVec_.assign (static_cast<size_t> (std::max (1, au.cond)), 0.0f);
    embMix_.assign (static_cast<size_t> (std::max (1, 2 * au.emb)), 0.0f);
    autoDecay_ = au.present ? std::exp (-(static_cast<double> (hop_) / sampleRate_) / au.avgSeconds) : 0.99;
    autoOn_ = false;

    target_ = source_ = 0;
    reset();
}

void SpecStream::reset()
{
    std::fill (ring_.begin(), ring_.end(), 0.0f);
    ringPos_ = 0;
    for (auto& h : hist_)
        std::fill (h.begin(), h.end(), 0.0f);
    std::fill (histPos_.begin(), histPos_.end(), 0);
    std::fill (delta_.begin(), delta_.end(), 0.0);
    std::fill (buf_.begin(), buf_.end(), 0.0f);
    bufPos_ = taps_ - 1;
    hopPos_ = 0;
    hopIndex_ = 0;
    havePrev_ = false;
    std::fill (autoW_.begin(), autoW_.end(), 1.0 / static_cast<double> (autoW_.size()));
    std::fill (autoAcc_.begin(), autoAcc_.end(), 0.0);
    autoCount_ = 0.0;
}

void SpecStream::setCondition (int target, int source)
{
    target_ = std::clamp (target, 0, model_->numTargets - 1);
    source_ = std::clamp (source, 0, model_->numSources - 1);
}

void SpecStream::analyse()
{
    const float g = analysisGain_;
    const int skip = nfft_ - winLen_; // the ring holds nfft_ samples, the window uses the newest winLen_ of them
    for (int n = 0; n < winLen_; ++n)
    {
        const size_t idx = static_cast<size_t> ((ringPos_ + skip + n) & (nfft_ - 1)); // oldest sample first
        re_[static_cast<size_t> (n)] = static_cast<double> (ring_[idx] * g) * window_[static_cast<size_t> (n)];
        im_[static_cast<size_t> (n)] = 0.0;
    }
    for (int n = winLen_; n < nfft_; ++n)
    {
        re_[static_cast<size_t> (n)] = 0.0;
        im_[static_cast<size_t> (n)] = 0.0;
    }
    fftA_.forward (re_.data(), im_.data());
    for (int b = 0; b < bands_; ++b)
    {
        double sum = 0.0;
        const int lo = bandLo_[static_cast<size_t> (b)], hi = bandHi_[static_cast<size_t> (b)];
        for (int k = lo; k <= hi; ++k)
            sum += (re_[static_cast<size_t> (k)] * re_[static_cast<size_t> (k)] + im_[static_cast<size_t> (k)] * im_[static_cast<size_t> (k)]) / windowNorm_;
        bandDb_[static_cast<size_t> (b)] = 10.0 * std::log10 (sum / (hi - lo + 1) + 1e-14);
    }
}

void SpecStream::runNet()
{
    const SpecModel& m = *model_;
    const size_t ch = static_cast<size_t> (ch_);

    // features: band shape (mean over the range removed), then a level cue
    double mean = 0.0, power = 0.0;
    for (int b = 0; b < bands_; ++b)
    {
        mean += bandDb_[static_cast<size_t> (b)] * rangeMask_[static_cast<size_t> (b)];
        power += std::pow (10.0, bandDb_[static_cast<size_t> (b)] / 10.0);
    }
    mean /= rangeCount_;
    for (int b = 0; b < bands_; ++b)
        feat_[static_cast<size_t> (b)] = static_cast<float> (std::clamp (bandDb_[static_cast<size_t> (b)] - mean, -60.0, 30.0) / 20.0);
    const double levelDb = 10.0 * std::log10 (power + 1e-14);
    feat_[static_cast<size_t> (bands_)] = static_cast<float> (std::clamp ((levelDb + 30.0) / 30.0, -3.0, 2.0));

    const size_t nIn = static_cast<size_t> (bands_ + 1);
    for (size_t c = 0; c < ch; ++c)
        h_[c] = m.inB[c] + dotProduct (m.inW.data() + c * nIn, feat_.data(), static_cast<int> (nIn));

    const float* film = autoOn_ ? filmBuf_.data() : m.filmFor (target_, source_);
    std::fill (skip_.begin(), skip_.end(), 0.0f);
    for (int bi = 0; bi < nBlocks_; ++bi)
    {
        const SpmBlock& blk = m.blocks[static_cast<size_t> (bi)];
        auto& hist = hist_[static_cast<size_t> (bi)];
        const int d = blk.dilation;
        const int slots = 2 * d + 1;
        int& pos = histPos_[static_cast<size_t> (bi)];
        std::memcpy (hist.data() + static_cast<size_t> (pos) * ch, h_.data(), ch * sizeof (float)); // block input at time t
        const float* x2 = hist.data() + static_cast<size_t> ((pos - 2 * d + 2 * slots) % slots) * ch; // x[t - 2d]
        const float* x1 = hist.data() + static_cast<size_t> ((pos - d + slots) % slots) * ch;         // x[t - d]
        const float* x0 = hist.data() + static_cast<size_t> (pos) * ch;                              // x[t]
        pos = (pos + 1) % slots;

        const float* gamma = film + static_cast<size_t> (bi) * 4 * ch;
        const float* beta = gamma + 2 * ch;
        const int c = static_cast<int> (ch);
        for (size_t o = 0; o < 2 * ch; ++o)
        {
            const float* w = blk.conv.data() + o * ch * 3; // [in][tap]
            float acc = blk.convB[o];
            for (size_t i = 0; i < ch; ++i)
                acc += w[i * 3 + 0] * x2[i] + w[i * 3 + 1] * x1[i] + w[i * 3 + 2] * x0[i];
            z_[o] = acc * (1.0f + gamma[o]) + beta[o];
        }
        for (size_t o = 0; o < ch; ++o)
            tmp_[o] = std::tanh (z_[o]) * sigmoidf (z_[ch + o]);
        for (size_t o = 0; o < ch; ++o)
        {
            h_[o] += blk.resB[o] + dotProduct (blk.res.data() + o * ch, tmp_.data(), c);
            skip_[o] += blk.skipB[o] + dotProduct (blk.skip.data() + o * ch, tmp_.data(), c);
        }
        // the block input history above holds x (before the residual update); h_ is now the next block's input
    }

    for (size_t o = 0; o < ch; ++o)
        tmp_[o] = siluf (skip_[o]);
    const float md = m.maxDeltaDb;
    for (int j = 0; j < m.nCtrl; ++j)
    {
        const float v = m.headB[static_cast<size_t> (j)] + dotProduct (m.headW.data() + static_cast<size_t> (j) * ch, tmp_.data(), ch_);
        ctrl_[static_cast<size_t> (j)] = md * std::tanh (v / md);
    }
    double dmean = 0.0;
    for (int b = 0; b < bands_; ++b)
    {
        const int i0 = ctrlIdx_[static_cast<size_t> (b)];
        const double f = ctrlFrac_[static_cast<size_t> (b)];
        delta_[static_cast<size_t> (b)] = (1.0 - f) * ctrl_[static_cast<size_t> (i0)] + f * ctrl_[static_cast<size_t> (i0) + 1];
        dmean += delta_[static_cast<size_t> (b)] * rangeMask_[static_cast<size_t> (b)];
    }
    dmean /= rangeCount_;
    for (int b = 0; b < bands_; ++b)
        delta_[static_cast<size_t> (b)] -= dmean;
}

void SpecStream::designFir (std::vector<float>& rev)
{
    const int half = dfft_ / 2;
    const double ln10over20 = std::log (10.0) / 20.0;
    for (int k = 0; k <= half; ++k)
    {
        const int i0 = designIdx_[static_cast<size_t> (k)];
        const double f = designFrac_[static_cast<size_t> (k)];
        const double g = (1.0 - f) * gains_[static_cast<size_t> (i0)] + f * gains_[static_cast<size_t> (i0) + 1];
        re_[static_cast<size_t> (k)] = g * ln10over20;
        if (k > 0 && k < half)
            re_[static_cast<size_t> (dfft_ - k)] = g * ln10over20;
    }
    std::fill (im_.begin(), im_.begin() + dfft_, 0.0);
    fftD_.inverse (re_.data(), im_.data());          // real cepstrum
    for (int n = 1; n < half; ++n)
        re_[static_cast<size_t> (n)] *= 2.0;         // fold to a causal cepstrum
    for (int n = half + 1; n < dfft_; ++n)
        re_[static_cast<size_t> (n)] = 0.0;
    std::fill (im_.begin(), im_.begin() + dfft_, 0.0);
    fftD_.forward (re_.data(), im_.data());
    for (int k = 0; k < dfft_; ++k)
    {
        const double mag = std::exp (re_[static_cast<size_t> (k)]);
        const double ph = im_[static_cast<size_t> (k)];
        re_[static_cast<size_t> (k)] = mag * std::cos (ph);
        im_[static_cast<size_t> (k)] = mag * std::sin (ph);
    }
    fftD_.inverse (re_.data(), im_.data());
    const int fade = std::min (64, taps_ / 4);
    for (int i = 0; i < taps_; ++i)
    {
        double w = 1.0;
        const int fromEnd = taps_ - 1 - i;
        if (fromEnd < fade)
            w = 0.5 * (1.0 - std::cos (kPi * fromEnd / fade));
        rev[static_cast<size_t> (taps_ - 1 - i)] = static_cast<float> (re_[static_cast<size_t> (i)] * w); // time reversed for the dot product
    }
}

// Running spectrum of the loud frames -> soft weights over the source cells (twin of AutoSource in spec_numpy.py).
void SpecStream::updateAuto()
{
    const SpmAuto& au = model_->autoSource;
    double total = 0.0;
    for (int b = 0; b < bands_; ++b)
        total += std::pow (10.0, bandDb_[static_cast<size_t> (b)] / 10.0);
    if (10.0 * std::log10 (total + 1e-14) <= static_cast<double> (au.gateDb))
        return; // quiet frame (silence between notes): keep what was learned
    for (int b = 0; b < bands_; ++b)
        autoAcc_[static_cast<size_t> (b)] = autoAcc_[static_cast<size_t> (b)] * autoDecay_ + std::pow (10.0, bandDb_[static_cast<size_t> (b)] / 10.0);
    autoCount_ = autoCount_ * autoDecay_ + 1.0;
    if (autoCount_ < au.minFrames)
        return;

    // spectrum shape of the running mean: dB, 3-band smoothing in power (edges repeated), mean removed inside the mask
    std::vector<double>& p = autoMix_; // scratch, bands_ values
    for (int b = 0; b < bands_; ++b)
        p[static_cast<size_t> (b)] = autoAcc_[static_cast<size_t> (b)] / autoCount_ + 1e-14; // mean power of the loud frames
    size_t k = 0;
    double mean = 0.0;
    for (int b = 0; b < bands_; ++b)
    {
        if (! au.mask[static_cast<size_t> (b)])
            continue;
        const double a = p[static_cast<size_t> (std::max (b - 1, 0))], c = p[static_cast<size_t> (b)], d = p[static_cast<size_t> (std::min (b + 1, bands_ - 1))];
        autoFeat_[k] = 10.0 * std::log10 ((a + c + d) / 3.0 + 1e-14);
        mean += autoFeat_[k++];
    }
    mean /= au.nMask;
    for (int i = 0; i < au.nMask; ++i)
        autoFeat_[static_cast<size_t> (i)] -= mean;

    const int S = model_->numSources;
    double dMin = 1.0e30;
    for (int s = 0; s < S; ++s)
    {
        const float* pr = au.protos.data() + static_cast<size_t> (s) * static_cast<size_t> (au.nMask);
        double sum = 0.0;
        for (int i = 0; i < au.nMask; ++i)
        {
            const double d = static_cast<double> (pr[i]) - autoFeat_[static_cast<size_t> (i)];
            sum += d * d;
        }
        autoW_[static_cast<size_t> (s)] = std::sqrt (sum / au.nMask); // distance for now
        dMin = std::min (dMin, autoW_[static_cast<size_t> (s)]);
    }
    double norm = 0.0;
    for (int s = 0; s < S; ++s)
    {
        autoW_[static_cast<size_t> (s)] = std::exp (-(autoW_[static_cast<size_t> (s)] - dMin) / static_cast<double> (au.tauDb));
        norm += autoW_[static_cast<size_t> (s)];
    }
    for (auto& w : autoW_)
        w /= norm;
}

// FiLM vectors of every block for the MIXED source embedding (the conditioning layer is not linear, so it runs here).
void SpecStream::autoFilm()
{
    const SpecModel& m = *model_;
    const SpmAuto& au = m.autoSource;
    const size_t E = static_cast<size_t> (au.emb), C = static_cast<size_t> (au.cond), S = static_cast<size_t> (m.numSources);
    for (size_t e = 0; e < E; ++e)
    {
        double v = 0.0;
        for (size_t s = 0; s < S; ++s)
            v += autoW_[s] * static_cast<double> (au.embSrc[s * E + e]);
        embMix_[e] = static_cast<float> (v);
        embMix_[E + e] = au.embTgt[static_cast<size_t> (target_) * E + e];
    }
    for (size_t o = 0; o < C; ++o)
        condVec_[o] = siluf (au.condB[o] + dotProduct (au.condW.data() + o * 2 * E, embMix_.data(), static_cast<int> (2 * E)));
    const size_t n = static_cast<size_t> (4 * ch_);
    for (size_t b = 0; b < static_cast<size_t> (nBlocks_); ++b)
        for (size_t o = 0; o < n; ++o)
            filmBuf_[b * n + o] = au.filmB[b][o] + dotProduct (au.filmW[b].data() + o * C, condVec_.data(), static_cast<int> (C));
}

void SpecStream::startHop()
{
    const SpecModel& m = *model_;
    const bool haveFrame = hopIndex_ >= framesPerWindow_;
    if (haveFrame)
    {
        analyse();
        if (autoOn_)
        {
            updateAuto();
            autoFilm();
        }
        runNet();
    }
    const float* eq = m.eqFor (target_, source_);
    if (autoOn_)
    {
        const size_t S = static_cast<size_t> (m.numSources);
        for (int b = 0; b < bands_; ++b)
        {
            double v = 0.0;
            for (size_t s = 0; s < S; ++s)
                v += autoW_[s] * static_cast<double> (m.eqFor (target_, static_cast<int> (s))[b]);
            autoMix_[static_cast<size_t> (b)] = v;
        }
    }
    for (int b = 0; b < bands_; ++b)
        gains_[static_cast<size_t> (b)] = ((autoOn_ ? autoMix_[static_cast<size_t> (b)] : static_cast<double> (eq[b]))
                                           + (haveFrame ? delta_[static_cast<size_t> (b)] : 0.0))
                                          * static_cast<double> (intensity_);
    std::swap (curRev_, prevRev_);
    designFir (curRev_);
    havePrev_ = hopIndex_ > 0;
    ++hopIndex_;
}

void SpecStream::process (const float* in, float* out, int numSamples)
{
    const int mask = nfft_ - 1;
    for (int i = 0; i < numSamples; ++i)
    {
        if (hopPos_ == 0)
            startHop();

        const float x = in[i];
        ring_[static_cast<size_t> (ringPos_)] = x;
        ringPos_ = (ringPos_ + 1) & mask;

        if (bufPos_ == static_cast<int> (buf_.size()))
        {
            std::memmove (buf_.data(), buf_.data() + buf_.size() - static_cast<size_t> (taps_ - 1),
                          static_cast<size_t> (taps_ - 1) * sizeof (float));
            bufPos_ = taps_ - 1;
        }
        buf_[static_cast<size_t> (bufPos_)] = x;
        const float* w = buf_.data() + (bufPos_ - (taps_ - 1));
        ++bufPos_;

        const float yNew = dotProduct (curRev_.data(), w, taps_);
        if (havePrev_)
        {
            const float yOld = dotProduct (prevRev_.data(), w, taps_);
            out[i] = yOld + ramp_[static_cast<size_t> (hopPos_)] * (yNew - yOld);
        }
        else
            out[i] = yNew;

        if (++hopPos_ == hop_)
            hopPos_ = 0;
    }
}
} // namespace fourreal
