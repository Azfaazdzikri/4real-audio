#include "GuitarModels.h"

namespace fourreal
{
namespace
{
// Pickup order inside every model: bridge first, neck last (a middle pickup sits between).
constexpr unsigned kBridge2 = 1u, kNeck2 = 2u;                 // two-pickup guitars
constexpr unsigned kBridge3 = 1u, kMid3 = 2u, kNeck3 = 4u;     // three-pickup guitars

// Stratocaster selector positions that exist in the recorded data (the middle-only position 3 was not
// recorded), bridge -> neck. The names keep the real 5-way numbering.
std::vector<PositionDef> stratPositions()
{
    return { { "1 Bridge", kBridge3 },
             { "2 Bridge+Middle", kBridge3 | kMid3 },
             { "4 Middle+Neck", kMid3 | kNeck3 },
             { "5 Neck", kNeck3 } };
}

GuitarModel makeStrat (const char* code, const char* id, const char* name, PickupDef bridge)
{
    GuitarModel m;
    m.code = code;
    m.id = id;
    m.displayName = name;
    m.accentRgb = 0xff4fd1c5;
    m.potOhm = 250e3;
    m.toneCapF = 22e-9;
    m.pickups = { bridge, { "Middle single coil", 2.3, 5.9e3 }, { "Neck single coil", 2.2, 5.8e3 } };
    m.positions = stratPositions();
    m.volumes = { { "VOLUME", -1 } };
    m.tones = { { "TONE NECK", -1, kNeck3 }, { "TONE MIDDLE", -1, kMid3 } };
    return m;
}

// 3-way toggle positions that exist in the recorded data. keep: bit 0 = bridge, bit 1 = both/middle, bit 2 = neck.
constexpr unsigned kKeepBridge = 1u, kKeepBoth = 2u, kKeepNeck = 4u, kKeepAll = 7u;

std::vector<PositionDef> togglePositions (const char* middleName, unsigned keep)
{
    std::vector<PositionDef> all;
    if (keep & kKeepBridge) all.push_back ({ "Bridge", kBridge2 });
    if (keep & kKeepBoth)   all.push_back ({ middleName, kBridge2 | kNeck2 });
    if (keep & kKeepNeck)   all.push_back ({ "Neck", kNeck2 });
    return all;
}

// Les Paul style: each pickup has its own volume and tone.
GuitarModel makeIndependent (const char* code, const char* id, const char* name, unsigned accent,
                             double potOhm, double capF, PickupDef bridge, PickupDef neck, unsigned keep)
{
    GuitarModel m;
    m.code = code;
    m.id = id;
    m.displayName = name;
    m.accentRgb = accent;
    m.potOhm = potOhm;
    m.toneCapF = capF;
    m.pickups = { bridge, neck };
    m.positions = togglePositions ("Middle (both)", keep);
    m.volumes = { { "VOL NECK", 1 }, { "VOL BRIDGE", 0 } };
    m.tones = { { "TONE NECK", 1, 0 }, { "TONE BRIDGE", 0, 0 } };
    return m;
}

// Telecaster / PRS style: one master volume and one master tone.
GuitarModel makeMaster (const char* code, const char* id, const char* name, unsigned accent,
                        double potOhm, double capF, PickupDef bridge, PickupDef neck,
                        const char* middleName, unsigned keep)
{
    GuitarModel m;
    m.code = code;
    m.id = id;
    m.displayName = name;
    m.accentRgb = accent;
    m.potOhm = potOhm;
    m.toneCapF = capF;
    m.pickups = { bridge, neck };
    m.positions = togglePositions (middleName, keep);
    m.volumes = { { "VOLUME", -1 } };
    m.tones = { { "TONE", -1, kBridge2 | kNeck2 } };
    return m;
}

std::vector<GuitarModel> buildModels()
{
    std::vector<GuitarModel> models;

    // --- The 8 recorded guitars. Only selector positions present in the training data are listed;
    // --- the order is also the order of the source list (the first entry is the default source).
    models.push_back (makeIndependent ("LP", "les-paul", "LP", 0xffd9a441, 500e3, 22e-9,
                                       { "Bridge humbucker", 5.5, 8.5e3 }, { "Neck humbucker", 4.5, 7.5e3 },
                                       kKeepAll));  // recorded: Bridge, Middle, Neck

    models.push_back (makeIndependent ("335", "es-335", "335", 0xffc8553d, 500e3, 22e-9,
                                       { "Bridge humbucker", 5.2, 8.2e3 }, { "Neck humbucker", 4.5, 7.5e3 },
                                       kKeepNeck));  // recorded: Neck only

    // SG values are assumed equal to the Les Paul ones until measured.
    models.push_back (makeIndependent ("SG", "sg", "SG", 0xffa03030, 500e3, 22e-9,
                                       { "Bridge humbucker", 5.5, 8.5e3 }, { "Neck humbucker", 4.5, 7.5e3 },
                                       kKeepBridge | kKeepNeck));  // recorded: Bridge, Neck

    models.push_back (makeMaster ("PRS", "prs-se-custom-24", "SE24", 0xffd0782b, 500e3, 22e-9,
                                  { "Bridge humbucker", 4.9, 8.6e3 }, { "Neck humbucker", 4.3, 7.6e3 },
                                  "Middle (both)", kKeepAll));  // recorded: Bridge, Middle, Neck

    // Rickenbacker 330: "hi-gain" single coils, volume + tone per pickup.
    {
        auto m = makeIndependent ("RIC", "rickenbacker-330", "R330", 0xff3fa7c9, 250e3, 22e-9,
                                  { "Bridge hi-gain single coil", 3.3, 8.0e3 },
                                  { "Neck hi-gain single coil", 3.0, 7.2e3 },
                                  kKeepBridge);  // recorded: Bridge only
        models.push_back (std::move (m));
    }

    models.push_back (makeStrat ("STR", "strat-sss", "STR", { "Bridge single coil", 2.6, 6.6e3 }));

    models.push_back (makeMaster ("TCR", "telecaster", "TLR", 0xffe5b64b, 250e3, 47e-9,
                                  { "Bridge single coil", 2.8, 7.6e3 }, { "Neck single coil", 2.5, 6.8e3 },
                                  "Bridge+Neck", kKeepBridge | kKeepNeck));  // recorded: Bridge, Neck

    // Gretsch Falcon: Filter'Tron humbuckers, a volume per pickup, a master volume and a master tone.
    {
        GuitarModel m;
        m.code = "FLC";
        m.id = "gretsch-falcon";
        m.displayName = "White Falcon";
        m.accentRgb = 0xff2f7fd0;
        m.potOhm = 500e3;
        m.toneCapF = 22e-9;
        m.pickups = { { "Bridge Filter'Tron", 3.0, 4.9e3 }, { "Neck Filter'Tron", 2.8, 4.6e3 } };
        m.positions = togglePositions ("Middle (both)", kKeepBoth);  // recorded: Middle (both) only
        m.volumes = { { "VOL NECK", 1 }, { "VOL BRIDGE", 0 }, { "MASTER", -1 } };
        m.tones = { { "TONE", -1, kBridge2 | kNeck2 } };
        models.push_back (std::move (m));
    }

    // Every entry can also be chosen as the SOURCE guitar in the plugin: 17 recorded cells in total.
    return models;
}
} // namespace

const std::vector<GuitarModel>& allModels()
{
    static const std::vector<GuitarModel> models = buildModels();
    return models;
}

const GuitarModel* findModelByCode (const std::string& code)
{
    for (const auto& m : allModels())
        if (code == m.code)
            return &m;
    return nullptr;
}

} // namespace fourreal
