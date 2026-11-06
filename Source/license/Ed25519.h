#pragma once

#include <cstddef>
#include <cstdint>

// Ed25519 signature VERIFICATION (and SHA-512), no dependencies, no JUCE. The plugin only ever verifies: the private key
// lives on the licence server and never ships in the plugin.
//
// The arithmetic follows the public-domain TweetNaCl design (16 limbs of 16 bits, constant structure, no tables), and
// every constant comes from tools/license/gen_ed25519_tables.py. It is checked against the RFC 8032 test vectors and
// against signatures made by OpenSSL (tests/license_test.cpp, tests/license/gen_vectors.py). Verification works on public
// data only, so constant-time behaviour is not needed here.
namespace fourreal::license
{
void sha512 (const uint8_t* data, size_t size, uint8_t out[64]);

// true only if `signature` is a valid Ed25519 signature of message[0..size) under `publicKey` (and S is canonical).
bool ed25519Verify (const uint8_t signature[64], const uint8_t* message, size_t size, const uint8_t publicKey[32]);
} // namespace fourreal::license
