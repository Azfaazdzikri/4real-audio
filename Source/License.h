#pragma once

#include "Library.h"

// Licensing of the library guitars: every guitar has its own licence id (LibraryEntry::licenseId), so a user can own
// some guitars and not others, like the piano models of a piano app.
//
// TEST BUILD: licensing is not implemented yet and every guitar is unlocked (kUnlockEverything). The audio code already
// asks isUnlocked() for the guitar it plays, and the editor already has a locked look, so adding real licences later
// only means replacing the body of isUnlocked() (read the entitlements the user activated, verify their signature)
// and adding an activation dialog. Nothing else has to change.
namespace fourreal
{
class LicenseManager
{
public:
    static constexpr bool kUnlockEverything = true;

    // Cheap and thread safe (called from the audio thread once per processing slice).
    static bool isUnlocked (const LibraryEntry&)
    {
        return kUnlockEverything;
    }
};
} // namespace fourreal
