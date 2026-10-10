#include "GuitarModels.h"

namespace fourreal
{
namespace
{
// Pickup order inside every model: bridge first, neck last (a middle pickup sits between).
constexpr unsigned kBridge2 = 1u, kNeck2 = 2u;                 // two-pickup guitars
constexpr unsigned kBridge3 = 1u, kMid3 = 2u, kNeck3 = 4u;     // three-pickup guitars

// Typical 5-way Stratocaster selector, bridge -> neck.
std::vector<PositionDef> stratPositions()
{
    return { { "1 Bridge", kBridge3 },
             { "2 Bridge+Middle", kBridge3 | kMid3 },
             { "3 Middle", kMid3 },
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

std::vector<PositionDef> toggle3 (const char* middleName)
{
    return { { "Bridge", kBridge2 }, { middleName, kBridge2 | kNeck2 }, { "Neck", kNeck2 } };
}

// Les Paul style: each pickup has its own volume and tone.
GuitarModel makeIndependent (const char* code, const char* id, const char* name, unsigned accent,
                             double potOhm, double capF, PickupDef bridge, PickupDef neck)
{
    GuitarModel m;
    m.code = code;
    m.id = id;
    m.displayName = name;
    m.accentRgb = accent;
    m.potOhm = potOhm;
    m.toneCapF = capF;
    m.pickups = { bridge, neck };
    m.positions = toggle3 ("Middle (both)");
    m.volumes = { { "VOL NECK", 1 }, { "VOL BRIDGE", 0 } };
    m.tones = { { "TONE NECK", 1, 0 }, { "TONE BRIDGE", 0, 0 } };
    return m;
}

// Telecaster / PRS style: one master volume and one master tone.
GuitarModel makeMaster (const char* code, const char* id, const char* name, unsigned accent,
                        double potOhm, double capF, PickupDef bridge, PickupDef neck,
                        const char* middleName)
{
    GuitarModel m;
    m.code = code;
    m.id = id;
    m.displayName = name;
    m.accentRgb = accent;
    m.potOhm = potOhm;
    m.toneCapF = capF;
    m.pickups = { bridge, neck };
    m.positions = toggle3 (middleName);
    m.volumes = { { "VOLUME", -1 } };
    m.tones = { { "TONE", -1, kBridge2 | kNeck2 } };
    return m;
}

std::vector<GuitarModel> buildModels()
{
    std::vector<GuitarModel> models;

    // --- Source-only: the player's own guitar (local-brand Strat-style, humbucker at the bridge).
    // The humbucker values are a guess: measure your own pickup and replace them.
    {
        auto m = makeStrat ("", "strat-hss", "Strat HSS (my guitar)", { "Bridge humbucker", 4.0, 9.0e3 });
        m.accentRgb = 0xff8a8f9a;
        models.push_back (std::move (m));
    }

    // --- Plugin targets ---------------------------------------------------------------------------
    models.push_back (makeIndependent ("LP", "les-paul", "Les Paul", 0xffd9a441, 500e3, 22e-9,
                                       { "Bridge humbucker", 5.5, 8.5e3 }, { "Neck humbucker", 4.5, 7.5e3 }));

    models.push_back (makeIndependent ("335", "es-335", "ES-335", 0xffc8553d, 500e3, 22e-9,
                                       { "Bridge humbucker", 5.2, 8.2e3 }, { "Neck humbucker", 4.5, 7.5e3 }));

    // SG values are assumed equal to the Les Paul ones until measured.
    models.push_back (makeIndependent ("SG", "sg", "SG", 0xffa03030, 500e3, 22e-9,
                                       { "Bridge humbucker", 5.5, 8.5e3 }, { "Neck humbucker", 4.5, 7.5e3 }));

    models.push_back (makeMaster ("PRS", "prs-se-custom-24", "PRS SE Custom 24", 0xffd0782b, 500e3, 22e-9,
                                  { "Bridge humbucker", 4.9, 8.6e3 }, { "Neck humbucker", 4.3, 7.6e3 },
                                  "Middle (both)"));

    // Rickenbacker 330: "hi-gain" single coils, volume + tone per pickup.
    {
        auto m = makeIndependent ("RIC", "rickenbacker-330", "Rickenbacker 330", 0xff3fa7c9, 250e3, 22e-9,
                                  { "Bridge hi-gain single coil", 3.3, 8.0e3 },
                                  { "Neck hi-gain single coil", 3.0, 7.2e3 });
        models.push_back (std::move (m));
    }

    models.push_back (makeStrat ("STR", "strat-sss", "Stratocaster (SSS)", { "Bridge single coil", 2.6, 6.6e3 }));

    models.push_back (makeMaster ("TCR", "telecaster", "Telecaster", 0xffe5b64b, 250e3, 47e-9,
                                  { "Bridge single coil", 2.8, 7.6e3 }, { "Neck single coil", 2.5, 6.8e3 },
                                  "Bridge+Neck"));

    // Gretsch Falcon: Filter'Tron humbuckers, a volume per pickup, a master volume and a master tone.
    {
        GuitarModel m;
        m.code = "FLC";
        m.id = "gretsch-falcon";
        m.displayName = "Gretsch Falcon (G6136TG)";
        m.accentRgb = 0xff2f7fd0;
        m.potOhm = 500e3;
        m.toneCapF = 22e-9;
        m.pickups = { { "Bridge Filter'Tron", 3.0, 4.9e3 }, { "Neck Filter'Tron", 2.8, 4.6e3 } };
        m.positions = toggle3 ("Middle (both)");
        m.volumes = { { "VOL NECK", 1 }, { "VOL BRIDGE", 0 }, { "MASTER", -1 } };
        m.tones = { { "TONE", -1, kBridge2 | kNeck2 } };
        models.push_back (std::move (m));
    }

    // Every entry can also be chosen as the SOURCE guitar in the plugin.
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
