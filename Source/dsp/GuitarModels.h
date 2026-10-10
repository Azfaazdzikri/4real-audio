#pragma once

#include <string>
#include <vector>

#include "GuitarCircuit.h"

// Data-driven description of a guitar's electronics: pickups, selector positions, volume
// and tone controls. One description drives both the SOURCE guitar (what the player has)
// and the TARGET guitar (the plugin variant: LP, 335, SG, PRS, RIC, STR, TCR, FLC).
//
// Component values are TYPICAL ESTIMATES for each guitar family, not measurements of a
// specific instrument (see docs/GUITAR_MODELS.md). Measure DCR/inductance and replace them.
// No JUCE dependency.
namespace fourreal
{
struct PickupDef
{
    const char* name;
    double inductanceH;
    double dcrOhm;
};

struct PositionDef
{
    const char* name;
    unsigned pickupMask; // bit i = pickup i is connected in this selector position
};

// Volume pot. pickup >= 0: sits right after that pickup (independent volume, Les Paul style).
// pickup == -1: master volume after the selector (Strat, Telecaster, PRS).
struct VolumeDef
{
    const char* label;
    int pickup;
};

// Tone pot + capacitor. pickup >= 0: attached to that pickup. pickup == -1: attached to the
// shared bus, and only in circuit while at least one pickup in activeMask is selected
// (Strat: neck tone only works with the neck pickup, middle tone with the middle pickup).
struct ToneDef
{
    const char* label;
    int pickup;
    unsigned activeMask;
};

struct GuitarModel
{
    const char* code;        // plugin short code: LP, 335, SG, PRS, RIC, STR, TCR, FLC ("" = source only)
    const char* id;          // stable identifier
    const char* displayName; // shown in the UI
    unsigned accentRgb;      // UI accent colour
    double potOhm;
    double toneCapF;
    std::vector<PickupDef> pickups;
    std::vector<PositionDef> positions; // ordered bridge -> neck
    std::vector<VolumeDef> volumes;
    std::vector<ToneDef> tones;
};

struct ModelSettings
{
    int position = 0;               // index into GuitarModel::positions
    std::vector<double> volumes;    // 0..10 per VolumeDef (missing entries = 10)
    std::vector<double> tones;      // 0..10 per ToneDef (missing entries = 10)
};

// All known guitars. The first entry (Strat HSS) is the default source guitar.
const std::vector<GuitarModel>& allModels();

// Finds a model by plugin code ("LP", ...). Returns nullptr if there is none.
const GuitarModel* findModelByCode (const std::string& code);

// Complex transfer function (string signal -> amp input) for the given model and settings.
std::vector<cplx> modelResponse (const std::vector<double>& freqsHz, const GuitarModel& model,
                                 const ModelSettings& settings, const Rig& rig = {});

} // namespace fourreal
