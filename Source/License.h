#pragma once

#include <atomic>
#include <cstdint>

#include "Library.h"
#include "license/LicenseConfig.h"

// Licensing of the library guitars: every guitar has its own licence (a code such as "LP" or "STR" in the signed token the
// licence server gives to a signed-in account on a registered computer), so a player can own some guitars and not others,
// like the piano models of a piano app.
//
// This header is the part the rest of the plugin uses and it has no JUCE dependency:
//   * isUnlocked (entry) is called from the audio thread once per processing slice and from the editor. It only reads one atomic
//     bit mask, so it is cheap and thread safe.
//   * The mask is written by LicenseClient (license/LicenseClient.h), which signs in, checks the token (Ed25519 signature,
//     computer, expiry, see license/Token.h) and keeps the token renewed.
//   * A guitar that is not unlocked passes the sound through untouched (PluginProcessor::processSlice) and shows LOCKED in
//     the library list.
// There is no unlocked build: every build enforces the licences.
namespace fourreal
{
class LicenseManager
{
public:
    static bool isUnlocked (const LibraryEntry& entry)
    {
        const auto index = static_cast<std::ptrdiff_t> (&entry - Library::get().entries().data());
        if (index < 0 || index >= 32)
            return false;
        return ((unlockedMask().load (std::memory_order_relaxed) >> index) & 1u) != 0;
    }

    // Bit i = library entry i (order of Library::entries()) is licensed. Written by LicenseClient.
    static void setUnlockedMask (uint32_t mask) { unlockedMask().store (mask, std::memory_order_relaxed); }
    static uint32_t getUnlockedMask() { return unlockedMask().load (std::memory_order_relaxed); }

private:
    static std::atomic<uint32_t>& unlockedMask()
    {
        static std::atomic<uint32_t> mask { 0 };
        return mask;
    }
};
} // namespace fourreal
