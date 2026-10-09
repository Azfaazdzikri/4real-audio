#pragma once

#include <algorithm>
#include <vector>

// Direct-form FIR that crossfades to new coefficients when they change (no clicks while
// turning knobs). All memory is allocated in prepare(); setTaps() and process() do not
// allocate, so they are safe on the audio thread. No JUCE dependency.
namespace fourreal
{
class CrossfadeFir
{
public:
    void prepare (int numChannels, int numTaps, int maxBlock, int fadeSamples = 2048)
    {
        numTaps_ = numTaps;
        maxBlock_ = maxBlock;
        fadeLen_ = std::max (1, fadeSamples);
        for (auto& h : hrev_)
            h.assign (static_cast<size_t> (numTaps), 0.0f);
        history_.assign (static_cast<size_t> (numChannels),
                         std::vector<float> (static_cast<size_t> (numTaps - 1), 0.0f));
        work_.assign (static_cast<size_t> (numTaps - 1 + maxBlock), 0.0f);
        active_ = 0;
        fading_ = false;
        fadePos_ = 0;
        hrev_[0][static_cast<size_t> (numTaps - 1)] = 1.0f; // identity
    }

    // Copies numTaps coefficients (h[0] first) and fades to them.
    void setTaps (const float* h)
    {
        if (fading_) // finish the previous fade instantly
        {
            active_ = 1 - active_;
            fading_ = false;
        }
        auto& dst = hrev_[1 - active_];
        for (int j = 0; j < numTaps_; ++j)
            dst[static_cast<size_t> (j)] = h[numTaps_ - 1 - j];
        fading_ = true;
        fadePos_ = 0;
    }

    void process (float* const* data, int numChannels, int numSamples)
    {
        int done = 0;
        while (done < numSamples)
        {
            const int len = std::min (maxBlock_, numSamples - done);
            const float* a = hrev_[static_cast<size_t> (active_)].data();
            const float* b = hrev_[static_cast<size_t> (1 - active_)].data();
            const bool fading = fading_;
            const int fadeStart = fadePos_;
            const size_t histLen = static_cast<size_t> (numTaps_ - 1);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* x = data[ch] + done;
                auto& hist = history_[static_cast<size_t> (ch)];
                std::copy (hist.begin(), hist.end(), work_.begin());
                std::copy (x, x + len, work_.begin() + static_cast<std::ptrdiff_t> (histLen));

                for (int n = 0; n < len; ++n)
                {
                    const float* w = work_.data() + n;
                    const float ya = dot (a, w, numTaps_);
                    if (fading)
                    {
                        const float mix = std::min (1.0f, static_cast<float> (fadeStart + n) / static_cast<float> (fadeLen_));
                        x[n] = ya + mix * (dot (b, w, numTaps_) - ya);
                    }
                    else
                    {
                        x[n] = ya;
                    }
                }
                std::copy (work_.begin() + len, work_.begin() + len + static_cast<std::ptrdiff_t> (histLen), hist.begin());
            }

            if (fading_)
            {
                fadePos_ += len;
                if (fadePos_ >= fadeLen_)
                {
                    active_ = 1 - active_;
                    fading_ = false;
                }
            }
            done += len;
        }
    }

private:
    static float dot (const float* h, const float* w, int n)
    {
        float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        int j = 0;
        for (; j + 4 <= n; j += 4)
        {
            a0 += h[j] * w[j];
            a1 += h[j + 1] * w[j + 1];
            a2 += h[j + 2] * w[j + 2];
            a3 += h[j + 3] * w[j + 3];
        }
        for (; j < n; ++j)
            a0 += h[j] * w[j];
        return (a0 + a1) + (a2 + a3);
    }

    std::vector<float> hrev_[2]; // time-reversed taps
    std::vector<std::vector<float>> history_;
    std::vector<float> work_;
    int numTaps_ = 1, maxBlock_ = 1, fadeLen_ = 1, fadePos_ = 0, active_ = 0;
    bool fading_ = false;
};
} // namespace fourreal
