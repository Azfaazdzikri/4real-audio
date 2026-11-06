#include "Token.h"

#include <algorithm>

#include "../dsp/MiniJson.h"
#include "Ed25519.h"

namespace fourreal::license
{
bool Entitlement::hasGuitar (const std::string& code) const
{
    return std::find (guitars.begin(), guitars.end(), code) != guitars.end();
}

const char* describe (TokenStatus s)
{
    switch (s)
    {
        case TokenStatus::Ok:           return "ok";
        case TokenStatus::Malformed:    return "malformed licence";
        case TokenStatus::BadSignature: return "licence signature is not valid";
        case TokenStatus::WrongVersion: return "licence format is too new, update the plugin";
        case TokenStatus::WrongMachine: return "licence belongs to another computer";
        case TokenStatus::Expired:      return "licence expired, connect to the internet to renew it";
        case TokenStatus::NotYetValid:  return "the clock of this computer is wrong";
    }
    return "";
}

bool base64urlDecode (const std::string& text, std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    size_t used = 0;
    for (char c : text)
    {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-' || c == '+') v = 62;
        else if (c == '_' || c == '/') v = 63;
        else if (c == '=') break; // padding: only at the end
        else return false;
        ++used;
        acc = (acc << 6) | static_cast<uint32_t> (v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back (static_cast<uint8_t> ((acc >> bits) & 0xff));
        }
    }
    // after the padding only more '=' may follow
    for (size_t i = used; i < text.size(); ++i)
        if (text[i] != '=')
            return false;
    if (used % 4 == 1) // one leftover character can never be valid
        return false;
    return true;
}

std::string base64urlEncode (const uint8_t* data, size_t size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string s;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < size; ++i)
    {
        acc = (acc << 8) | data[i];
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            s.push_back (alphabet[(acc >> bits) & 63]);
        }
    }
    if (bits > 0)
        s.push_back (alphabet[(acc << (6 - bits)) & 63]);
    return s;
}

bool decodePublicKey (const std::string& text, uint8_t out[32])
{
    std::vector<uint8_t> bytes;
    if (! base64urlDecode (text, bytes) || bytes.size() != 32)
        return false;
    std::copy (bytes.begin(), bytes.end(), out);
    return true;
}

namespace
{
bool readString (const minijson::Json& j, const char* key, std::string& out)
{
    const auto* v = j.find (key);
    if (v == nullptr || v->type != minijson::Json::Type::String)
        return false;
    out = v->text;
    return true;
}

bool readInt (const minijson::Json& j, const char* key, int64_t& out)
{
    const auto* v = j.find (key);
    if (v == nullptr || v->type != minijson::Json::Type::Number)
        return false;
    out = static_cast<int64_t> (v->number);
    return true;
}
} // namespace

TokenStatus verifyToken (const std::string& token, const uint8_t publicKey[32], const std::string& machineId, int64_t now,
                         Entitlement& out, int64_t allowedClockSkewSeconds)
{
    const auto dot = token.find ('.');
    if (dot == std::string::npos || dot == 0 || token.find ('.', dot + 1) != std::string::npos)
        return TokenStatus::Malformed;

    std::vector<uint8_t> payload, signature;
    if (! base64urlDecode (token.substr (0, dot), payload) || ! base64urlDecode (token.substr (dot + 1), signature)
        || payload.empty() || signature.size() != 64)
        return TokenStatus::Malformed;

    // The signature is checked BEFORE anything in the payload is trusted.
    if (! ed25519Verify (signature.data(), payload.data(), payload.size(), publicKey))
        return TokenStatus::BadSignature;

    Entitlement e;
    int64_t version = 0;
    try
    {
        const std::string text (payload.begin(), payload.end());
        minijson::Parser parser (text.data(), text.size());
        const auto json = parser.parse();
        if (json.type != minijson::Json::Type::Object)
            return TokenStatus::Malformed;
        if (! readInt (json, "v", version))
            return TokenStatus::Malformed;
        if (version != 1)
            return TokenStatus::WrongVersion;
        if (! readString (json, "sub", e.userId) || ! readString (json, "mid", e.machineId)
            || ! readInt (json, "iat", e.issuedAt) || ! readInt (json, "exp", e.expiresAt))
            return TokenStatus::Malformed;
        readString (json, "email", e.email);
        readString (json, "dev", e.deviceId);
        const auto* g = json.find ("guitars");
        // MiniJson keeps arrays of numbers in `nums`, so a numeric list would otherwise look like an empty one.
        if (g == nullptr || g->type != minijson::Json::Type::Array || ! g->nums.empty())
            return TokenStatus::Malformed;
        for (const auto& item : g->items)
        {
            if (item.type != minijson::Json::Type::String)
                return TokenStatus::Malformed;
            e.guitars.push_back (item.text);
        }
    }
    catch (const std::exception&)
    {
        return TokenStatus::Malformed;
    }

    if (e.machineId != machineId)
        return TokenStatus::WrongMachine;
    if (e.issuedAt > now + allowedClockSkewSeconds)
        return TokenStatus::NotYetValid;
    if (now >= e.expiresAt)
        return TokenStatus::Expired;

    out = std::move (e);
    return TokenStatus::Ok;
}
} // namespace fourreal::license
