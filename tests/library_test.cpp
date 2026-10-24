// Checks the guitar library: every library guitar has a loadable model, all models share the same source side, and every
// guitar has at least one selector position. Usage: library_test <models folder>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "GuitarModels.h"
#include "SpecModel.h"

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "models";
    int failures = 0;
    fourreal::SpecModel ref;
    bool haveRef = false;
    for (const auto& m : fourreal::allModels())
    {
        if (std::string (m.code).empty())
            continue;
        std::ifstream f (dir + "/" + m.code + "/model.spm", std::ios::binary);
        std::string bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        fourreal::SpecModel spec;
        std::string err;
        const bool ok = fourreal::SpecModel::load (bytes.data(), bytes.size(), spec, err);
        std::printf ("%-4s model %s%s\n", m.code, ok ? "ok" : "FAILED ", ok ? "" : err.c_str());
        failures += ok ? 0 : 1;
        if (m.positions.empty() || m.pickups.empty())
        {
            std::printf ("%-4s has no pickups / positions\n", m.code);
            ++failures;
        }
        if (ok && ! haveRef)
        {
            ref = spec;
            haveRef = true;
        }
        else if (ok && spec.numSources != ref.numSources)
        {
            std::printf ("%-4s source side differs from the first model\n", m.code);
            ++failures;
        }
    }
    std::printf (failures ? "FAILED\n" : "all ok\n");
    return failures ? 1 : 0;
}
