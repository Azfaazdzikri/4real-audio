#pragma once

#include <vector>

#include "NgmModel.h"

// Streaming (causal, sample-exact) inference of the conditional TCN stored in a .ngm file, one channel
// per NgmStream. All memory is allocated in prepare(); process() and setCondition() never allocate, so
// they are safe on the audio thread. No JUCE dependency.
//
// The per-pair EQ is NOT applied here: the caller filters the input with the EQ curve (eqFor()) first, and
// the stream adds the network's residual on top of that already-equalised signal:
//     out = x + inputScale * net(x / inputScale)
namespace fourreal
{
class NgmStream
{
public:
    static constexpr int kChunk = 512;

    // The model must outlive the stream.
    void prepare (const NgmModel& model);
    void reset();

    // Selects the (target position, source cell) pair. snap = true jumps at once (use when the stream
    // is silent / right after reset); otherwise the FiLM vectors glide over a few tens of ms.
    void setCondition (int target, int source, bool snap);

    // x -> x + residual. in and out may be the same buffer. Any numSamples.
    void process (const float* in, float* out, int numSamples);

private:
    void processChunk (const float* in, float* out, int n);

    const NgmModel* model_ = nullptr;
    int ch_ = 0;
    std::vector<int> hist_;               // per block: (kernel-1)*dilation
    std::vector<std::vector<float>> buf_; // per block: [ch][hist + kChunk] block input
    std::vector<float> acc_;              // [2ch][kChunk]
    std::vector<float> z_;                // [ch][kChunk]
    std::vector<float> skip_;             // [ch][kChunk]
    std::vector<float> tmp_;              // [ch][kChunk]
    std::vector<float> curFilm_, tgtFilm_, nxtFilm_; // [block][4ch]
    std::vector<const float*> srcs_;     // pointer table for the multiply-accumulate kernel
    float glide_ = 0.2f;
};

// Minimum-phase FIR (kVoicingTaps taps) for the per-pair EQ curve of (target, source) at the given sample rate.
// The curve is stored on 64 bands (log-spaced); like in training it is interpolated linearly in log-frequency and
// held beyond the first / last band centre.
std::vector<float> designEqTaps (const NgmModel& model, int target, int source, double sampleRate);

} // namespace fourreal
