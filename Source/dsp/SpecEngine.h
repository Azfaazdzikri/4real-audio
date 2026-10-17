#pragma once

#include <vector>

#include "SpecModel.h"

// Streaming spectral-envelope engine (one channel per SpecStream), the C++ twin of training/nn/spec_numpy.py.
//
// Every hop (512 samples) it
//   1. takes the last 4096 input samples (the frame that just completed), computes the 64-band power spectrum (dB),
//   2. runs the small causal conv net on that frame -> dynamic correction curve (dB per band),
//   3. adds the static per-pair EQ, multiplies by the intensity, and designs a 1024-tap minimum-phase FIR from the
//      resulting gain curve,
//   4. filters the next hop with a linear crossfade from the previous hop's filter to the new one.
// The output is therefore always the input filtered: no distortion, no added noise, no latency. Hop j uses frame j - 8
// (the net needs the whole analysis window), so the first 8 hops use the static EQ only.
// All memory is allocated in prepare(); process() and setCondition() never allocate (audio thread safe). No JUCE dependency.
namespace fourreal
{
// Small radix-2 complex FFT (double), forward unnormalised, inverse divided by n.
class Fft
{
public:
    void prepare (int n);
    void forward (double* re, double* im) const { run (re, im, false); }
    void inverse (double* re, double* im) const { run (re, im, true); }

private:
    void run (double* re, double* im, bool inverse) const;
    int n_ = 0;
    std::vector<int> rev_;
    std::vector<double> cos_, sin_;
};

class SpecStream
{
public:
    // The model must outlive the stream. sampleRate is the host rate: bands are defined in Hz, so other rates than the
    // 48 kHz the model was trained at still land on the right frequencies (the analysis window just gets a different length).
    void prepare (const SpecModel& model, double sampleRate);
    void reset();

    // Selects the (target position, source cell) pair; takes effect at the next hop.
    void setCondition (int target, int source);
    void setIntensity (float v) { intensity_ = v; }
    // Linear gain applied to the signal before the analysis only (auto level); the filter acts on the raw input.
    void setAnalysisGain (float g) { analysisGain_ = g; }

    // in and out may be the same buffer. Any numSamples.
    void process (const float* in, float* out, int numSamples);

    // Gain curve (dB per band) used for the most recently started hop (after intensity). For tests.
    const std::vector<double>& hopGains() const { return gains_; }

private:
    void startHop();
    void analyse();
    void runNet();
    void designFir (std::vector<float>& revTaps);

    const SpecModel* model_ = nullptr;
    int bands_ = 0, ch_ = 0, nfft_ = 0, hop_ = 0, taps_ = 0, dfft_ = 0, nBlocks_ = 0, framesPerWindow_ = 0;
    double sampleRate_ = 48000.0;
    int target_ = 0, source_ = 0;
    float intensity_ = 1.0f, analysisGain_ = 1.0f;

    Fft fftA_, fftD_;
    std::vector<double> window_;
    double windowNorm_ = 1.0;
    std::vector<int> bandLo_, bandHi_;     // bins of every band (inclusive)
    std::vector<double> rangeMask_;        // 1 for bands inside the zero-mean range
    double rangeCount_ = 1.0;
    std::vector<int> designIdx_;           // per design-FFT bin: lower band index of the interpolation
    std::vector<double> designFrac_;       // and the weight of the upper band
    std::vector<int> ctrlIdx_;             // per band: lower control point
    std::vector<double> ctrlFrac_;

    // analysis
    std::vector<float> ring_;
    int ringPos_ = 0;
    std::vector<double> re_, im_, bandDb_;

    // net
    std::vector<float> feat_, h_, z_, skip_, tmp_, ctrl_;
    std::vector<std::vector<float>> hist_; // per block: (2*dilation + 1) frames of the block input
    std::vector<int> histPos_;
    std::vector<double> delta_, gains_;

    // filtering
    std::vector<float> curRev_, prevRev_, buf_, ramp_;
    int bufPos_ = 0, hopPos_ = 0, hopIndex_ = 0;
    bool havePrev_ = false;
};
} // namespace fourreal
