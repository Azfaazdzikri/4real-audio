#pragma once

#include <cstddef>
#include <string>
#include <vector>

// .spm (spectral model) file loader. A .spm file is JSON: it holds the small causal conv net shared by every pair, the
// band layout, and the pre-evaluated conditioning (FiLM vectors and the static EQ curve) for every
// (target position, source cell) pair. One file per TARGET guitar. No JUCE dependency.
namespace fourreal
{
struct SpmBlock
{
    int dilation = 1;
    std::vector<float> conv;  // [2*ch][ch][kernel]  (PyTorch layout)
    std::vector<float> convB; // [2*ch]
    std::vector<float> res;   // [ch][ch]
    std::vector<float> resB;  // [ch]
    std::vector<float> skip;  // [ch][ch]
    std::vector<float> skipB; // [ch]
};

struct SpmSourceInfo
{
    std::string code; // guitar code (LP, 335, ...)
    int position = 0; // index within that guitar's recorded positions, bridge -> neck
    std::string cell;
};

struct SpecModel
{
    std::string name, targetCode;
    double sampleRate = 48000.0;
    float referenceRmsDb = -30.0f;

    int channels = 32, kernel = 3, bands = 64, nCtrl = 20, nfft = 4096, hop = 512, taps = 1024, designFft = 8192;
    float maxDeltaDb = 8.0f, eqLimitDb = 15.0f, rangeLoHz = 150.0f, rangeHiHz = 8000.0f;
    std::vector<double> bandEdgesHz; // bands + 1

    std::vector<float> inW, inB; // [ch][bands+1], [ch]
    std::vector<SpmBlock> blocks;
    std::vector<float> headW, headB; // [nCtrl][ch], [nCtrl]

    int numTargets = 0, numSources = 0;
    std::vector<SpmSourceInfo> sources;
    std::vector<std::string> targetCells; // cell name of every target position
    std::vector<float> film; // [target][source][block][4*ch]
    std::vector<float> eqDb; // [target][source][bands], shape only (zero mean over the range)

    const float* filmFor (int target, int source) const
    {
        return film.data() + ((static_cast<size_t> (target) * static_cast<size_t> (numSources) + static_cast<size_t> (source))
                              * blocks.size() * static_cast<size_t> (4 * channels));
    }
    const float* eqFor (int target, int source) const
    {
        return eqDb.data() + ((static_cast<size_t> (target) * static_cast<size_t> (numSources) + static_cast<size_t> (source))
                              * static_cast<size_t> (bands));
    }

    // Index of the source with this guitar code and position, or -1.
    int findSource (const std::string& code, int position) const;

    // Parses a .spm document. Returns false and fills `error` on any problem.
    static bool load (const char* data, size_t size, SpecModel& out, std::string& error);
};
} // namespace fourreal
