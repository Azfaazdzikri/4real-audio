// Licence core tests: Ed25519 verification and SHA-512 against OpenSSL vectors, token parsing and checks.
// Build: g++ -std=c++20 -O2 -ISource tests/license_test.cpp Source/license/Ed25519.cpp Source/license/Token.cpp -o license_test
// Run:   ./license_test tests/license/vectors.txt tests/license/token_cases.txt
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "license/Ed25519.h"
#include "license/Token.h"

using namespace fourreal::license;

namespace
{
int failures = 0;
int checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

std::vector<uint8_t> fromHex (const std::string& s)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        out.push_back (static_cast<uint8_t> (std::stoi (s.substr (i, 2), nullptr, 16)));
    return out;
}

std::string toHex (const uint8_t* d, size_t n)
{
    static const char* h = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i)
    {
        s.push_back (h[d[i] >> 4]);
        s.push_back (h[d[i] & 15]);
    }
    return s;
}

void runVectors (const char* path)
{
    std::ifstream in (path);
    check (in.good(), std::string ("open ") + path);
    std::string line;
    int sigCases = 0, hashCases = 0, lineNo = 0;
    while (std::getline (in, line))
    {
        ++lineNo;
        std::istringstream ss (line);
        std::string kind;
        ss >> kind;
        if (kind == "SIG")
        {
            std::string pub, msg, sig;
            int expect = 0;
            ss >> pub >> msg >> sig;
            if (! (ss >> expect))
                expect = 0;
            const auto p = fromHex (pub), m = fromHex (msg == "-" ? "" : msg), s = fromHex (sig);
            if (p.size() != 32 || s.size() != 64)
            {
                check (false, "malformed signature vector on line " + std::to_string (lineNo));
                continue;
            }
            const bool got = ed25519Verify (s.data(), m.data(), m.size(), p.data());
            check (got == (expect == 1), "signature vector line " + std::to_string (lineNo) + " expected " + std::to_string (expect));
            ++sigCases;
        }
        else if (kind == "HASH")
        {
            std::string msg, digest;
            ss >> msg >> digest;
            const auto m = fromHex (msg == "-" ? "" : msg);
            uint8_t out[64];
            sha512 (m.data(), m.size(), out);
            check (toHex (out, 64) == digest, "sha512 vector line " + std::to_string (lineNo));
            ++hashCases;
        }
    }
    std::cout << "vectors: " << sigCases << " signature cases, " << hashCases << " sha512 cases\n";
}

// token_cases.txt:  KEY <pub b64>   MID <machine>   NOW <unix>   then lines  CASE <name> <expected status name> <token>
const char* statusName (TokenStatus s)
{
    switch (s)
    {
        case TokenStatus::Ok: return "Ok";
        case TokenStatus::Malformed: return "Malformed";
        case TokenStatus::BadSignature: return "BadSignature";
        case TokenStatus::WrongVersion: return "WrongVersion";
        case TokenStatus::WrongMachine: return "WrongMachine";
        case TokenStatus::Expired: return "Expired";
        case TokenStatus::NotYetValid: return "NotYetValid";
    }
    return "?";
}

void runTokens (const char* path)
{
    std::ifstream in (path);
    check (in.good(), std::string ("open ") + path);
    uint8_t key[32] = {};
    std::string mid;
    int64_t now = 0;
    std::string line;
    int cases = 0;
    while (std::getline (in, line))
    {
        std::istringstream ss (line);
        std::string kind;
        ss >> kind;
        if (kind == "KEY")
        {
            std::string b64;
            ss >> b64;
            check (decodePublicKey (b64, key), "decode public key");
        }
        else if (kind == "MID")
            ss >> mid;
        else if (kind == "NOW")
            ss >> now;
        else if (kind == "CASE")
        {
            std::string name, expected, token, guitars;
            ss >> name >> expected >> guitars >> token;
            Entitlement e;
            const auto status = verifyToken (token, key, mid, now, e);
            check (expected == statusName (status), "token case '" + name + "': expected " + expected + ", got " + statusName (status));
            if (status == TokenStatus::Ok)
            {
                std::string joined;
                for (const auto& g : e.guitars)
                    joined += (joined.empty() ? "" : ",") + g;
                check (joined == (guitars == "-" ? "" : guitars), "token case '" + name + "': guitars '" + joined + "' != '" + guitars + "'");
                check (e.machineId == mid, "token case '" + name + "': machine id");
            }
            ++cases;
        }
    }
    std::cout << "tokens: " << cases << " cases\n";
}

void runBase64()
{
    const std::vector<std::pair<std::string, std::string>> good = {
        { "", "" }, { "Zg", "f" }, { "Zg==", "f" }, { "Zm8", "fo" }, { "Zm9v", "foo" }, { "Zm9vYg", "foob" }, { "Zm9vYmE=", "fooba" }, { "Zm9vYmFy", "foobar" }
    };
    for (const auto& [enc, dec] : good)
    {
        std::vector<uint8_t> out;
        check (base64urlDecode (enc, out) && std::string (out.begin(), out.end()) == dec, "base64 decode '" + enc + "'");
    }
    std::vector<uint8_t> out;
    check (! base64urlDecode ("Z", out), "base64 one leftover character is rejected");
    check (! base64urlDecode ("Zm9v!", out), "base64 bad character is rejected");
    check (! base64urlDecode ("Zg==x", out), "base64 data after padding is rejected");
    const uint8_t raw[] = { 0xfb, 0xff, 0xfe, 0x00, 0x01 };
    const auto enc = base64urlEncode (raw, sizeof raw);
    check (enc == "-__-AAE", "base64url encode alphabet: got " + enc);
    check (base64urlDecode (enc, out) && out.size() == sizeof raw && std::memcmp (out.data(), raw, sizeof raw) == 0, "base64url round trip");
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "usage: license_test vectors.txt token_cases.txt\n";
        return 2;
    }
    runBase64();
    runVectors (argv[1]);
    runTokens (argv[2]);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
