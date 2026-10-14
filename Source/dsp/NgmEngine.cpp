#include "NgmEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "FilterDesign.h"

namespace fourreal
{
namespace
{
// Rational tanh (7/6 Pade), accurate to ~1e-6 below |x| = 4.97 and held at +-1 beyond; vectorises (no libm call).
inline float fastTanh (float x)
{
    x = std::min (4.97f, std::max (-4.97f, x));
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / den;
}
inline float fastSigmoid (float x) { return 0.5f + 0.5f * fastTanh (0.5f * x); }
inline float silu (float x) { return x * fastSigmoid (x); }

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define NGM_SSE 1
#include <emmintrin.h>
#endif

// Two output rows at once:  y0[t] += sum_k w0[k] * src[k][t],  y1[t] += sum_k w1[k] * src[k][t].
// Tiles of 16 samples are accumulated in registers, so every source load feeds two rows. This is the whole
// cost of the network (~20k multiply-adds per sample), so it is written with SSE2 intrinsics (baseline on x64)
// instead of relying on the compiler's auto-vectoriser.
inline void accumulate2 (float* y0, float* y1, const float* const* src, const float* w0, const float* w1, int terms, int n)
{
    int t = 0;
#if NGM_SSE
    for (; t + 16 <= n; t += 16)
    {
        __m128 a0 = _mm_loadu_ps (y0 + t), a1 = _mm_loadu_ps (y0 + t + 4), a2 = _mm_loadu_ps (y0 + t + 8), a3 = _mm_loadu_ps (y0 + t + 12);
        __m128 b0 = _mm_loadu_ps (y1 + t), b1 = _mm_loadu_ps (y1 + t + 4), b2 = _mm_loadu_ps (y1 + t + 8), b3 = _mm_loadu_ps (y1 + t + 12);
        for (int k = 0; k < terms; ++k)
        {
            const float* s = src[k] + t;
            const __m128 va = _mm_set1_ps (w0[k]), vb = _mm_set1_ps (w1[k]);
            const __m128 s0 = _mm_loadu_ps (s), s1 = _mm_loadu_ps (s + 4), s2 = _mm_loadu_ps (s + 8), s3 = _mm_loadu_ps (s + 12);
            a0 = _mm_add_ps (a0, _mm_mul_ps (va, s0)); b0 = _mm_add_ps (b0, _mm_mul_ps (vb, s0));
            a1 = _mm_add_ps (a1, _mm_mul_ps (va, s1)); b1 = _mm_add_ps (b1, _mm_mul_ps (vb, s1));
            a2 = _mm_add_ps (a2, _mm_mul_ps (va, s2)); b2 = _mm_add_ps (b2, _mm_mul_ps (vb, s2));
            a3 = _mm_add_ps (a3, _mm_mul_ps (va, s3)); b3 = _mm_add_ps (b3, _mm_mul_ps (vb, s3));
        }
        _mm_storeu_ps (y0 + t, a0); _mm_storeu_ps (y0 + t + 4, a1); _mm_storeu_ps (y0 + t + 8, a2); _mm_storeu_ps (y0 + t + 12, a3);
        _mm_storeu_ps (y1 + t, b0); _mm_storeu_ps (y1 + t + 4, b1); _mm_storeu_ps (y1 + t + 8, b2); _mm_storeu_ps (y1 + t + 12, b3);
    }
#endif
    for (; t < n; ++t)
    {
        float a = y0[t], b = y1[t];
        for (int k = 0; k < terms; ++k)
        {
            const float v = src[k][t];
            a += w0[k] * v;
            b += w1[k] * v;
        }
        y0[t] = a;
        y1[t] = b;
    }
}
} // namespace

void NgmStream::prepare (const NgmModel& model)
{
    model_ = &model;
    ch_ = model.channels;
    const size_t ch = static_cast<size_t> (ch_);
    hist_.clear();
    buf_.clear();
    for (const auto& b : model.blocks)
    {
        const int h = (model.kernel - 1) * b.dilation;
        hist_.push_back (h);
        buf_.emplace_back (ch * static_cast<size_t> (h + kChunk), 0.0f);
    }
    acc_.assign (2 * ch * kChunk, 0.0f);
    z_.assign (ch * kChunk, 0.0f);
    skip_.assign (ch * kChunk, 0.0f);
    tmp_.assign (ch * kChunk, 0.0f);
    const size_t filmSize = static_cast<size_t> (model.numBlocks) * 4 * ch;
    curFilm_.assign (filmSize, 0.0f);
    tgtFilm_.assign (filmSize, 0.0f);
    nxtFilm_.assign (filmSize, 0.0f);
    srcs_.assign (static_cast<size_t> (ch_ * model.kernel), nullptr);
    reset();
}

void NgmStream::reset()
{
    for (auto& b : buf_)
        std::fill (b.begin(), b.end(), 0.0f);
    curFilm_ = tgtFilm_;
}

void NgmStream::setCondition (int target, int source, bool snap)
{
    const float* f = model_->filmFor (target, source);
    std::copy (f, f + tgtFilm_.size(), tgtFilm_.begin());
    if (snap)
        curFilm_ = tgtFilm_;
    // glide time constant ~40 ms per chunk step, independent of how the host splits blocks
    glide_ = 1.0f - std::exp (-static_cast<float> (kChunk) / (0.040f * static_cast<float> (model_->sampleRate)));
}

void NgmStream::process (const float* in, float* out, int numSamples)
{
    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (kChunk, numSamples - done);
        processChunk (in + done, out + done, n);
        done += n;
    }
}

void NgmStream::processChunk (const float* in, float* out, int n)
{
    const NgmModel& m = *model_;
    const int ch = ch_;
    const int K = m.kernel;
    const float invScale = 1.0f / m.inputScale;

    // film glide for this chunk: linear ramp from cur to nxt
    for (size_t i = 0; i < curFilm_.size(); ++i)
    {
        const float d = tgtFilm_[i] - curFilm_[i];
        nxtFilm_[i] = std::abs (d) < 1e-6f ? tgtFilm_[i] : curFilm_[i] + d * glide_;
    }

    // input conv1x1 (1 -> ch) into block 0's buffer
    {
        float* b0 = buf_[0].data();
        const int stride = hist_[0] + kChunk;
        for (int c = 0; c < ch; ++c)
        {
            float* row = b0 + static_cast<size_t> (c) * static_cast<size_t> (stride) + hist_[0];
            const float w = m.inW[static_cast<size_t> (c)] * invScale, b = m.inB[static_cast<size_t> (c)];
            for (int t = 0; t < n; ++t)
                row[t] = w * in[t] + b;
        }
    }

    std::fill (skip_.begin(), skip_.begin() + static_cast<std::ptrdiff_t> (ch) * kChunk, 0.0f);

    for (int bi = 0; bi < m.numBlocks; ++bi)
    {
        const NgmBlock& blk = m.blocks[static_cast<size_t> (bi)];
        const int d = blk.dilation;
        const int H = hist_[static_cast<size_t> (bi)];
        const int stride = H + kChunk;
        float* x = buf_[static_cast<size_t> (bi)].data(); // [ch][stride], current samples start at +H

        // gated conv: acc[o] = bias + sum_i sum_tap W[o][i][tap] * x[i][t - (K-1-tap)*d]
        const int convTerms = ch * K;
        for (int i = 0; i < ch; ++i)
            for (int tap = 0; tap < K; ++tap)
                srcs_[static_cast<size_t> (i * K + tap)] = x + static_cast<size_t> (i) * static_cast<size_t> (stride) + H - (K - 1 - tap) * d;
        for (int o = 0; o < 2 * ch; o += 2)
        {
            float* a0 = acc_.data() + static_cast<size_t> (o) * kChunk;
            float* a1 = a0 + kChunk;
            std::fill (a0, a0 + n, blk.convB[static_cast<size_t> (o)]);
            std::fill (a1, a1 + n, blk.convB[static_cast<size_t> (o) + 1]);
            accumulate2 (a0, a1, srcs_.data(), blk.conv.data() + static_cast<size_t> (o) * static_cast<size_t> (convTerms),
                         blk.conv.data() + static_cast<size_t> (o + 1) * static_cast<size_t> (convTerms), convTerms, n);
        }

        // FiLM + gate: z = tanh(a') * sigmoid(b'),  h' = h * (1 + gamma) + beta
        const float* f0 = curFilm_.data() + static_cast<size_t> (bi) * 4 * static_cast<size_t> (ch);
        const float* f1 = nxtFilm_.data() + static_cast<size_t> (bi) * 4 * static_cast<size_t> (ch);
        const float invN = 1.0f / static_cast<float> (n);
        for (int c = 0; c < ch; ++c)
        {
            float* pa = acc_.data() + static_cast<size_t> (c) * kChunk;
            float* pb = acc_.data() + static_cast<size_t> (ch + c) * kChunk;
            float* z = z_.data() + static_cast<size_t> (c) * kChunk;
            const float ga0 = 1.0f + f0[c], ga1 = 1.0f + f1[c];
            const float gb0 = 1.0f + f0[ch + c], gb1 = 1.0f + f1[ch + c];
            const float ba0 = f0[2 * ch + c], ba1 = f1[2 * ch + c];
            const float bb0 = f0[3 * ch + c], bb1 = f1[3 * ch + c];
            const float dga = (ga1 - ga0) * invN, dgb = (gb1 - gb0) * invN;
            const float dba = (ba1 - ba0) * invN, dbb = (bb1 - bb0) * invN;
            for (int t = 0; t < n; ++t)
            {
                const float ft = static_cast<float> (t);
                const float a = pa[t] * (ga0 + dga * ft) + (ba0 + dba * ft);
                const float b = pb[t] * (gb0 + dgb * ft) + (bb0 + dbb * ft);
                z[t] = fastTanh (a) * fastSigmoid (b);
            }
        }

        // res 1x1 -> next block input (plus the block input itself), skip 1x1 -> accumulator.
        // The last block's residual output is never used (as in the PyTorch model), so it is skipped.
        const bool last = bi + 1 == m.numBlocks;
        float* nextBuf = last ? nullptr : buf_[static_cast<size_t> (bi) + 1].data();
        const int nextOff = last ? 0 : hist_[static_cast<size_t> (bi) + 1];
        const int nextStride = nextOff + kChunk;
        for (int j = 0; j < ch; ++j)
            srcs_[static_cast<size_t> (j)] = z_.data() + static_cast<size_t> (j) * kChunk;
        if (! last)
        {
            for (int o = 0; o < ch; ++o)
            {
                float* dst = nextBuf + static_cast<size_t> (o) * static_cast<size_t> (nextStride) + nextOff;
                const float* xin = x + static_cast<size_t> (o) * static_cast<size_t> (stride) + H;
                const float rb = blk.resB[static_cast<size_t> (o)];
                for (int t = 0; t < n; ++t)
                    dst[t] = xin[t] + rb;
            }
        }
        for (int o = 0; o < ch; ++o)
        {
            float* sk = skip_.data() + static_cast<size_t> (o) * kChunk;
            const float sb = blk.skipB[static_cast<size_t> (o)];
            for (int t = 0; t < n; ++t)
                sk[t] += sb;
        }
        for (int o = 0; o < ch; o += 2)
        {
            float* s0 = skip_.data() + static_cast<size_t> (o) * kChunk;
            const size_t wo = static_cast<size_t> (o) * static_cast<size_t> (ch);
            if (! last)
            {
                float* d0 = nextBuf + static_cast<size_t> (o) * static_cast<size_t> (nextStride) + nextOff;
                float* d1 = d0 + nextStride;
                accumulate2 (d0, d1, srcs_.data(), blk.res.data() + wo, blk.res.data() + wo + static_cast<size_t> (ch), ch, n);
            }
            accumulate2 (s0, s0 + kChunk, srcs_.data(), blk.skip.data() + wo, blk.skip.data() + wo + static_cast<size_t> (ch), ch, n);
        }
    }

    // output net: SiLU -> 1x1 (ch->ch) -> SiLU -> 1x1 (ch->1), then out = x + scale * y
    for (int c = 0; c < ch; ++c)
    {
        float* sk = skip_.data() + static_cast<size_t> (c) * kChunk;
        for (int t = 0; t < n; ++t)
            sk[t] = silu (sk[t]);
    }
    float* y = acc_.data(); // reuse: one row
    std::fill (y, y + n, m.out1B);
    for (int o = 0; o < ch; ++o)
    {
        float* h = tmp_.data() + static_cast<size_t> (o) * kChunk;
        std::fill (h, h + n, m.out0B[static_cast<size_t> (o)]);
        for (int j = 0; j < ch; ++j)
            for (int t = 0; t < n; ++t)
                h[t] += m.out0W[static_cast<size_t> (o * ch + j)] * skip_[static_cast<size_t> (j) * kChunk + static_cast<size_t> (t)];
        const float w1 = m.out1W[static_cast<size_t> (o)];
        for (int t = 0; t < n; ++t)
            y[t] += w1 * silu (h[t]);
    }
    for (int t = 0; t < n; ++t)
        out[t] = in[t] + m.inputScale * y[t];

    // keep the last `hist` samples of every block input for the next chunk
    for (int bi = 0; bi < m.numBlocks; ++bi)
    {
        const int H = hist_[static_cast<size_t> (bi)];
        const int stride = H + kChunk;
        float* x = buf_[static_cast<size_t> (bi)].data();
        for (int c = 0; c < ch; ++c)
        {
            float* row = x + static_cast<size_t> (c) * static_cast<size_t> (stride);
            std::memmove (row, row + n, sizeof (float) * static_cast<size_t> (H));
        }
    }
    curFilm_ = nxtFilm_;
}

std::vector<float> designEqTaps (const NgmModel& model, int target, int source, double sampleRate)
{
    const float* eq = model.eqFor (target, source);
    const auto& c = model.bandCentersHz;
    const auto freqs = makeFrequencyGrid (sampleRate);
    std::vector<double> gain (freqs.size());
    const double l0 = std::log (c.front());
    const double step = std::log (c[1] / c[0]);
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        double pos = (std::log (freqs[i]) - l0) / step;
        pos = std::min (static_cast<double> (model.bands - 1), std::max (0.0, pos));
        const int i0 = static_cast<int> (std::floor (pos));
        const int i1 = std::min (model.bands - 1, i0 + 1);
        const double w = pos - i0;
        gain[i] = eq[i0] * (1.0 - w) + eq[i1] * w;
    }
    return designMinPhaseFir (freqs, gain, sampleRate);
}

} // namespace fourreal
