#pragma once

#include <complex>
#include <vector>

// Frequency-domain model of guitar electronics: pickups (inductance + DC resistance),
// volume pots, tone pots + capacitors, pickup selector, cable and amp input.
//
// This mirrors training/guitar_circuit.py (the Python prototype). Component values are
// typical published figures, not measurements of a specific guitar.
// No JUCE dependency, so it can be unit-tested on its own.
namespace fourreal
{
using cplx = std::complex<double>;

struct Pickup
{
    double inductanceH;
    double dcrOhm;
    double gain; // linear open-circuit output, estimated from inductance
};

Pickup makePickup (double inductanceH, double dcrOhm);

struct Rig
{
    double cablePf = 500.0;
    double ampInputOhm = 1.0e6;
};

enum class StratKind { SSS, HSS };

struct StratParams
{
    int position = 1; // 1 = bridge, 2 = bridge+mid, 3 = mid, 4 = mid+neck, 5 = neck
    double volume = 10.0;   // 0..10
    double toneNeck = 10.0; // 0..10
    double toneMid = 10.0;  // 0..10
};

enum class LesPaulSelector { Neck = 0, Both = 1, Bridge = 2 };

struct LesPaulParams
{
    LesPaulSelector selector = LesPaulSelector::Bridge;
    double volNeck = 10.0;
    double volBridge = 10.0;
    double toneNeck = 10.0;
    double toneBridge = 10.0;
};

// Complex transfer function (string signal -> amp input) at each frequency in Hz.
std::vector<cplx> stratResponse (const std::vector<double>& freqsHz, StratKind kind,
                                 const StratParams& params, const Rig& rig = {});

std::vector<cplx> lesPaulResponse (const std::vector<double>& freqsHz,
                                   const LesPaulParams& params, const Rig& rig = {});

} // namespace fourreal
