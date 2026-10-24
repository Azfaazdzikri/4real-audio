#include "Library.h"

#include <SpmData.h>

#include <cstring>

namespace fourreal
{
namespace
{
struct Blob
{
    const char* code;
    const char* data;
    size_t size;
};

// One embedded .spm per library guitar (CMakeLists.txt copies models/<CODE>/model.spm to guitar_<CODE>.spm so that the
// embedded symbol names do not collide).
const Blob kBlobs[] = {
    { "LP",  SpmData::guitar_LP_spm,  static_cast<size_t> (SpmData::guitar_LP_spmSize) },
    { "335", SpmData::guitar_335_spm, static_cast<size_t> (SpmData::guitar_335_spmSize) },
    { "SG",  SpmData::guitar_SG_spm,  static_cast<size_t> (SpmData::guitar_SG_spmSize) },
    { "PRS", SpmData::guitar_PRS_spm, static_cast<size_t> (SpmData::guitar_PRS_spmSize) },
    { "RIC", SpmData::guitar_RIC_spm, static_cast<size_t> (SpmData::guitar_RIC_spmSize) },
    { "STR", SpmData::guitar_STR_spm, static_cast<size_t> (SpmData::guitar_STR_spmSize) },
    { "TCR", SpmData::guitar_TCR_spm, static_cast<size_t> (SpmData::guitar_TCR_spmSize) },
    { "FLC", SpmData::guitar_FLC_spm, static_cast<size_t> (SpmData::guitar_FLC_spmSize) },
};
} // namespace

Library::Library()
{
    // The library lists the guitars in the order of allModels() (that is also the order of the source list).
    for (const auto& model : allModels())
    {
        LibraryEntry e;
        e.guitar = &model;
        e.licenseId = std::string ("4real.guitar.") + model.code;

        for (const auto& b : kBlobs)
        {
            if (std::strcmp (b.code, model.code) != 0)
                continue;
            std::string error;
            e.loaded = SpecModel::load (b.data, b.size, e.spec, error);
            break;
        }
        allLoaded_ = allLoaded_ && e.loaded;
        entries_.push_back (std::move (e));
    }

    // The source side must be identical in every model (see the comment in Library.h).
    const auto& ref = entries_.front().spec;
    for (const auto& e : entries_)
    {
        if (! e.loaded)
            continue;
        bool same = e.spec.numSources == ref.numSources
                    && e.spec.sources.size() == ref.sources.size()
                    && e.spec.autoSource.present == ref.autoSource.present
                    && e.spec.autoSource.enabled == ref.autoSource.enabled
                    && e.spec.autoSource.nMask == ref.autoSource.nMask;
        for (size_t i = 0; same && i < ref.sources.size(); ++i)
            same = e.spec.sources[i].code == ref.sources[i].code && e.spec.sources[i].position == ref.sources[i].position;
        if (! same)
            consistent_ = false;
    }
}

const Library& Library::get()
{
    static const Library library;
    return library;
}

int Library::indexOfCode (const std::string& code) const
{
    for (size_t i = 0; i < entries_.size(); ++i)
        if (code == entries_[i].guitar->code)
            return static_cast<int> (i);
    return -1;
}
} // namespace fourreal
