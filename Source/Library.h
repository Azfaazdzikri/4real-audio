#pragma once

#include <string>
#include <vector>

#include "dsp/GuitarModels.h"
#include "dsp/SpecModel.h"

// The guitar library: the single plugin / standalone holds every guitar the plugin can sound like (LP, 335, SG, PRS, RIC, STR,
// TCR, FLC), the same way a piano app holds several piano models. Each library guitar has
//   - its electronics description (pickups, selector, volume and tone knobs) from dsp/GuitarModels,
//   - its trained spectral model (a .spm file embedded in the plugin, see CMakeLists.txt),
//   - a licence id, so that every guitar can be unlocked separately later (see License.h).
// No JUCE dependency: everything is parsed once, the first time the library is used, and is read-only afterwards.
namespace fourreal
{
struct LibraryEntry
{
    const GuitarModel* guitar = nullptr;
    std::string licenseId;  // e.g. "4real.guitar.LP"
    SpecModel spec;
    bool loaded = false;

    const char* code() const { return guitar->code; }
    const char* displayName() const { return guitar->displayName; }
};

class Library
{
public:
    // Parses all embedded models on first use (thread safe). Takes a fraction of a second, so call it from the
    // plugin constructor, never from the audio thread.
    static const Library& get();

    int size() const { return static_cast<int> (entries_.size()); }
    const LibraryEntry& at (int index) const { return entries_[static_cast<size_t> (index < 0 ? 0 : (index >= size() ? size() - 1 : index))]; }
    const std::vector<LibraryEntry>& entries() const { return entries_; }

    // Index of the guitar with this code ("LP", ...), or -1.
    int indexOfCode (const std::string& code) const;

    // Every library model was trained against the same recorded source cells, so everything about the SOURCE side
    // (list of cells, Auto detection, "My guitar" calibration feature size) is the same for all of them and is read from the
    // first entry. consistent() is false if the embedded models ever disagree (a packaging mistake); the plugin then
    // refuses to share calibration data between guitars.
    const SpecModel& reference() const { return entries_.front().spec; }
    bool consistent() const { return consistent_; }
    bool allLoaded() const { return allLoaded_; }

private:
    Library();
    std::vector<LibraryEntry> entries_;
    bool consistent_ = true, allLoaded_ = true;
};
} // namespace fourreal
