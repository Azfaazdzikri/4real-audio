#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The licence token: what the licence server hands to a signed-in account on a registered computer. No JUCE dependency.
//
//   token   = base64url(payload) + "." + base64url(signature)
//   payload = UTF-8 JSON  {"v":1,"sub":"<account id>","email":"...","dev":"<device id>","mid":"<machine id>",
//                          "guitars":["LP","STR"],"iat":<unix seconds>,"exp":<unix seconds>}
//   signature = Ed25519 over the payload bytes, made with the server's private key.
//
// The plugin holds only the matching PUBLIC key, so it can check a token completely offline but cannot make one. A token is
// bound to one computer ("mid") and runs out ("exp"); the plugin renews it from the server whenever it is online.
namespace fourreal::license
{
struct Entitlement
{
    std::string userId, email, deviceId, machineId;
    std::vector<std::string> guitars; // library codes ("LP", "335", ...) this account may use on this computer
    int64_t issuedAt = 0, expiresAt = 0;

    bool hasGuitar (const std::string& code) const;
};

enum class TokenStatus
{
    Ok,
    Malformed,      // not a token, or the JSON is broken
    BadSignature,   // not made by our server (or modified)
    WrongVersion,
    WrongMachine,   // made for another computer
    Expired,
    NotYetValid     // issued in the future: the clock of this computer is wrong
};

const char* describe (TokenStatus);

// base64url (RFC 4648 section 5, with or without '=' padding) <-> bytes. The standard alphabet ('+' '/') is accepted too.
bool base64urlDecode (const std::string& text, std::vector<uint8_t>& out);
std::string base64urlEncode (const uint8_t* data, size_t size);

// Public key in base64/base64url (32 bytes). false if it is not exactly 32 bytes.
bool decodePublicKey (const std::string& text, uint8_t out[32]);

// Checks everything and fills `out` only when the result is Ok. `now` is the current unix time in seconds.
// `allowedClockSkewSeconds`: a token issued up to this far in the future is still accepted.
TokenStatus verifyToken (const std::string& token, const uint8_t publicKey[32], const std::string& machineId, int64_t now,
                         Entitlement& out, int64_t allowedClockSkewSeconds = 24 * 3600);
} // namespace fourreal::license
