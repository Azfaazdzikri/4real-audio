#pragma once

#include <cstddef>
#include <string>
#include <vector>

// .ngm (Neural Guitar Modeler) file loader. A .ngm file is JSON, like NAM's .nam: it holds the
// conditional TCN weights shared by every pair, plus pre-evaluated conditioning (FiLM vectors and
// the per-pair EQ curve) for every (target position, source cell). One file per TARGET guitar.
// No JUCE dependency.
namespace fourreal
{
struct NgmBlock
{
    int dilation = 1;
    std::vector<float> conv;  // [2*ch][ch][kernel]  (PyTorch layout)
    std::vector<float> convB; // [2*ch]
    std::vector<float> res;   // [ch][ch]
    std::vector<float> resB;  // [ch]
    std::vector<float> skip;  // [ch][ch]
    std::vector<float> skipB; // [ch]
};

struct NgmSourceInfo
{
    std::string code; // guitar code (LP, 335, ...)
    int position = 0; // index within that guitar's recorded positions, bridge -> neck
    std::string cell;
};

struct NgmModel
{
    std::string name, targetCode;
    double sampleRate = 48000.0;
    float inputScale = 0.03f;
    float referenceRmsDb = -30.0f;

    int channels = 16, kernel = 3, numBlocks = 10, bands = 64;
    std::vector<double> bandCentersHz;

    std::vector<float> inW, inB; // [ch]
    std::vector<NgmBlock> blocks;
    std::vector<float> out0W, out0B; // [ch][ch], [ch]
    std::vector<float> out1W;        // [ch]
    float out1B = 0.0f;

    int numTargets = 0, numSources = 0;
    std::vector<NgmSourceInfo> sources;
    std::vector<float> film; // [target][source][block][4*ch]
    std::vector<float> eqDb; // [target][source][bands]

    const float* filmFor (int target, int source) const
    {
        return film.data() + ((static_cast<size_t> (target) * static_cast<size_t> (numSources) + static_cast<size_t> (source))
                              * static_cast<size_t> (numBlocks) * static_cast<size_t> (4 * channels));
    }
    const float* eqFor (int target, int source) const
    {
        return eqDb.data() + ((static_cast<size_t> (target) * static_cast<size_t> (numSources) + static_cast<size_t> (source))
                              * static_cast<size_t> (bands));
    }

    // Index of the source with this guitar code and position, or -1.
    int findSource (const std::string& code, int position) const;

    // Receptive field in samples (history the network needs).
    int receptiveField() const;

    // Parses a .ngm document. Returns false and fills `error` on any problem.
    static bool load (const char* data, size_t size, NgmModel& out, std::string& error);
};

} // namespace fourreal
