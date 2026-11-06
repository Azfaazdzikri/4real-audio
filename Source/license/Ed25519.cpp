#include "Ed25519.h"

#include <cstring>
#include <vector>

#include "Ed25519Tables.inc"

namespace fourreal::license
{
// ---------------------------------------------------------------------------------------------------------------------
// SHA-512
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
inline uint64_t rotr (uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

void sha512Block (uint64_t h[8], const uint8_t* block)
{
    uint64_t w[80];
    for (int i = 0; i < 16; ++i)
    {
        uint64_t v = 0;
        for (int j = 0; j < 8; ++j)
            v = (v << 8) | block[i * 8 + j];
        w[i] = v;
    }
    for (int i = 16; i < 80; ++i)
    {
        const uint64_t s0 = rotr (w[i - 15], 1) ^ rotr (w[i - 15], 8) ^ (w[i - 15] >> 7);
        const uint64_t s1 = rotr (w[i - 2], 19) ^ rotr (w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 80; ++i)
    {
        const uint64_t S1 = rotr (e, 14) ^ rotr (e, 18) ^ rotr (e, 41);
        const uint64_t ch = (e & f) ^ (~e & g);
        const uint64_t t1 = hh + S1 + ch + kSha512K[i] + w[i];
        const uint64_t S0 = rotr (a, 28) ^ rotr (a, 34) ^ rotr (a, 39);
        const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint64_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
} // namespace

void sha512 (const uint8_t* data, size_t size, uint8_t out[64])
{
    uint64_t h[8];
    for (int i = 0; i < 8; ++i)
        h[i] = kSha512H0[i];

    size_t pos = 0;
    while (size - pos >= 128)
    {
        sha512Block (h, data + pos);
        pos += 128;
    }
    uint8_t tail[256] = {};
    const size_t rest = size - pos;
    if (rest > 0)
        std::memcpy (tail, data + pos, rest);
    tail[rest] = 0x80;
    const size_t total = rest + 1 + 16 <= 128 ? 128 : 256;
    const uint64_t bits = static_cast<uint64_t> (size) * 8u; // the high 64 bits of the 128-bit length stay 0
    for (int i = 0; i < 8; ++i)
        tail[total - 1 - i] = static_cast<uint8_t> (bits >> (8 * i));
    sha512Block (h, tail);
    if (total == 256)
        sha512Block (h, tail + 128);

    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            out[i * 8 + j] = static_cast<uint8_t> (h[i] >> (56 - 8 * j));
}

// ---------------------------------------------------------------------------------------------------------------------
// Field arithmetic mod 2^255 - 19 (TweetNaCl layout: 16 limbs, each nominally 16 bits, in int64)
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
using i64 = int64_t;
using gf = i64[16];

void set25519 (gf r, const gf a) { for (int i = 0; i < 16; ++i) r[i] = a[i]; }

void car25519 (gf o)
{
    for (int i = 0; i < 16; ++i)
    {
        o[i] += (i64 (1) << 16);
        const i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

void sel25519 (gf p, gf q, int b)
{
    const i64 c = ~(b - 1);
    for (int i = 0; i < 16; ++i)
    {
        const i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void pack25519 (uint8_t* o, const gf n)
{
    gf m, t;
    set25519 (t, n);
    car25519 (t);
    car25519 (t);
    car25519 (t);
    for (int j = 0; j < 2; ++j)
    {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i)
        {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = static_cast<int> ((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519 (t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i)
    {
        o[2 * i] = static_cast<uint8_t> (t[i] & 0xff);
        o[2 * i + 1] = static_cast<uint8_t> (t[i] >> 8);
    }
}

void unpack25519 (gf o, const uint8_t* n)
{
    for (int i = 0; i < 16; ++i)
        o[i] = n[2 * i] + (i64 (n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

void A (gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
void Z (gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }

void M (gf o, const gf a, const gf b)
{
    i64 t[31] = {};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i)
        t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i)
        o[i] = t[i];
    car25519 (o);
    car25519 (o);
}

void S (gf o, const gf a) { M (o, a, a); }

void inv25519 (gf o, const gf i)
{
    gf c;
    set25519 (c, i);
    for (int a = 253; a >= 0; --a)
    {
        S (c, c);
        if (a != 2 && a != 4)
            M (c, c, i);
    }
    set25519 (o, c);
}

void pow2523 (gf o, const gf i)
{
    gf c;
    set25519 (c, i);
    for (int a = 250; a >= 0; --a)
    {
        S (c, c);
        if (a != 1)
            M (c, c, i);
    }
    set25519 (o, c);
}

int neq25519 (const gf a, const gf b)
{
    uint8_t c[32], d[32];
    pack25519 (c, a);
    pack25519 (d, b);
    return std::memcmp (c, d, 32) != 0;
}

uint8_t par25519 (const gf a)
{
    uint8_t d[32];
    pack25519 (d, a);
    return d[0] & 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Edwards curve group (extended coordinates)
// ---------------------------------------------------------------------------------------------------------------------
struct Constants
{
    gf gf0 = {}, gf1 = {}, D = {}, D2 = {}, X = {}, Y = {}, I = {};
    Constants()
    {
        gf1[0] = 1;
        unpack25519 (D, kBytesD);
        unpack25519 (D2, kBytesD2);
        unpack25519 (X, kBytesBaseX);
        unpack25519 (Y, kBytesBaseY);
        unpack25519 (I, kBytesSqrtM1);
    }
};

const Constants& K()
{
    static const Constants c;
    return c;
}

void add (gf p[4], gf q[4])
{
    const auto& k = K();
    gf a, b, c, d, t, e, f, g, h;
    Z (a, p[1], p[0]);
    Z (t, q[1], q[0]);
    M (a, a, t);
    A (b, p[0], p[1]);
    A (t, q[0], q[1]);
    M (b, b, t);
    M (c, p[3], q[3]);
    M (c, c, k.D2);
    M (d, p[2], q[2]);
    A (d, d, d);
    Z (e, b, a);
    Z (f, d, c);
    A (g, d, c);
    A (h, b, a);
    M (p[0], e, f);
    M (p[1], h, g);
    M (p[2], g, f);
    M (p[3], e, h);
}

void cswap (gf p[4], gf q[4], uint8_t b)
{
    for (int i = 0; i < 4; ++i)
        sel25519 (p[i], q[i], b);
}

void pack (uint8_t* r, gf p[4])
{
    gf tx, ty, zi;
    inv25519 (zi, p[2]);
    M (tx, p[0], zi);
    M (ty, p[1], zi);
    pack25519 (r, ty);
    r[31] ^= par25519 (tx) << 7;
}

void scalarmult (gf p[4], gf q[4], const uint8_t* s)
{
    const auto& k = K();
    set25519 (p[0], k.gf0);
    set25519 (p[1], k.gf1);
    set25519 (p[2], k.gf1);
    set25519 (p[3], k.gf0);
    for (int i = 255; i >= 0; --i)
    {
        const uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        cswap (p, q, b);
        add (q, p);
        add (p, p);
        cswap (p, q, b);
    }
}

void scalarbase (gf p[4], const uint8_t* s)
{
    const auto& k = K();
    gf q[4];
    set25519 (q[0], k.X);
    set25519 (q[1], k.Y);
    set25519 (q[2], k.gf1);
    M (q[3], k.X, k.Y);
    scalarmult (p, q, s);
}

// Reduces the 64-byte number x mod L into r[32].
void modL (uint8_t* r, i64 x[64])
{
    i64 carry;
    int i, j;
    for (i = 63; i >= 32; --i)
    {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j)
        {
            x[j] += carry - 16 * x[i] * kBytesL[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j)
    {
        x[j] += carry - (x[31] >> 4) * kBytesL[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; ++j)
        x[j] -= carry * kBytesL[j];
    for (i = 0; i < 32; ++i)
    {
        x[i + 1] += x[i] >> 8;
        r[i] = static_cast<uint8_t> (x[i] & 255);
    }
}

void reduce (uint8_t r[64])
{
    i64 x[64];
    for (int i = 0; i < 64; ++i)
        x[i] = r[i];
    for (int i = 0; i < 64; ++i)
        r[i] = 0;
    modL (r, x);
}

// Decodes the public key and negates it (-A); false if it is not a point on the curve.
bool unpackneg (gf r[4], const uint8_t p[32])
{
    const auto& k = K();
    gf t, chk, num, den, den2, den4, den6;
    set25519 (r[2], k.gf1);
    unpack25519 (r[1], p);
    S (num, r[1]);
    M (den, num, k.D);
    Z (num, num, r[2]);
    A (den, r[2], den);

    S (den2, den);
    S (den4, den2);
    M (den6, den4, den2);
    M (t, den6, num);
    M (t, t, den);

    pow2523 (t, t);
    M (t, t, num);
    M (t, t, den);
    M (t, t, den);
    M (r[0], t, den);

    S (chk, r[0]);
    M (chk, chk, den);
    if (neq25519 (chk, num))
        M (r[0], r[0], k.I);

    S (chk, r[0]);
    M (chk, chk, den);
    if (neq25519 (chk, num))
        return false;

    if (par25519 (r[0]) == (p[31] >> 7))
        Z (r[0], k.gf0, r[0]);

    M (r[3], r[0], r[1]);
    return true;
}

// S must be < L, otherwise the signature is not in canonical form.
bool scalarIsCanonical (const uint8_t* s)
{
    for (int i = 31; i >= 0; --i)
    {
        if (s[i] < kBytesL[i])
            return true;
        if (s[i] > kBytesL[i])
            return false;
    }
    return false; // equal to L
}
} // namespace

bool ed25519Verify (const uint8_t signature[64], const uint8_t* message, size_t size, const uint8_t publicKey[32])
{
    if (! scalarIsCanonical (signature + 32))
        return false;

    gf q[4], p[4];
    if (! unpackneg (q, publicKey))
        return false;

    std::vector<uint8_t> buffer (64 + size);
    std::memcpy (buffer.data(), signature, 32);
    std::memcpy (buffer.data() + 32, publicKey, 32);
    if (size > 0)
        std::memcpy (buffer.data() + 64, message, size);
    uint8_t h[64];
    sha512 (buffer.data(), buffer.size(), h);
    reduce (h);

    scalarmult (p, q, h);
    gf b[4];
    scalarbase (b, signature + 32);
    add (p, b);

    uint8_t t[32];
    pack (t, p);
    uint8_t diff = 0;
    for (int i = 0; i < 32; ++i)
        diff |= static_cast<uint8_t> (t[i] ^ signature[i]);
    return diff == 0;
}
} // namespace fourreal::license
