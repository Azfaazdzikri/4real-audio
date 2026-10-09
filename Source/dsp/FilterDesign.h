#pragma once

#include <vector>

#include "GuitarCircuit.h"

// Turns guitar-circuit frequency responses into a minimum-phase FIR (no pre-ringing, no
// added latency beyond the filter's own short group delay). No JUCE dependency.
namespace fourreal
{
constexpr int kVoicingTaps = 512;

// Log-spaced analysis grid from 20 Hz up to min(20 kHz, ~Nyquist).
std::vector<double> makeFrequencyGrid (double sampleRate, int points = 400);

// dB curve that turns the source response into the target response, clipped.
std::vector<double> correctionDb (const std::vector<cplx>& source, const std::vector<cplx>& target,
                                  double maxBoostDb = 12.0, double maxCutDb = -30.0);

// Minimum-phase FIR matching gainDb at freqHz (linear interpolation in Hz, edges held).
std::vector<float> designMinPhaseFir (const std::vector<double>& freqHz, const std::vector<double>& gainDb,
                                      double sampleRate, int numTaps = kVoicingTaps);

// source -> target filter. If keepLevel is false the level difference at 1 kHz is removed.
std::vector<float> designVoicingTaps (const std::vector<cplx>& source, const std::vector<cplx>& target,
                                      const std::vector<double>& freqs, double sampleRate, bool keepLevel,
                                      int numTaps = kVoicingTaps);

std::vector<float> identityTaps (int numTaps = kVoicingTaps);

} // namespace fourreal
