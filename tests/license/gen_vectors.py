#!/usr/bin/env python3
"""Writes tests/license/vectors.txt for tests/license_test.cpp: signatures made by OpenSSL (python 'cryptography').
Line formats (hex):  SIG <pub> <msg or - for empty> <sig> <expect 1|0>     and     HASH <msg> <sha512>
Run:  python3 -I tests/license/gen_vectors.py > tests/license/vectors.txt
"""
import hashlib, os, random, sys
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

rng = random.Random(1234)
out = []

def pub_of(k):
    return k.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)

# RFC 8032 section 7.1 test vectors (secret keys, messages) - signatures are produced by OpenSSL, expected to match the RFC.
rfc = [
    ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", ""),
    ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", "72"),
    ("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", "af82"),
]
rfc_sigs = [
    "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
    None,  # RFC vector 2: no expected signature typed in, only checked against OpenSSL
    "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a",
]
for (sk_hex, msg_hex), expected in zip(rfc, rfc_sigs):
    k = Ed25519PrivateKey.from_private_bytes(bytes.fromhex(sk_hex))
    msg = bytes.fromhex(msg_hex)
    sig = k.sign(msg)
    if expected is not None:
        assert sig.hex() == expected, "OpenSSL signature differs from RFC 8032 vector"
    out.append("SIG %s %s %s 1" % (pub_of(k).hex(), msg_hex or "-", sig.hex()))

def flip(b, i, bit=0):
    b = bytearray(b); b[i] ^= (1 << bit); return bytes(b)

for n in range(60):
    k = Ed25519PrivateKey.generate()
    pub = pub_of(k)
    msg = os.urandom(rng.choice([0, 1, 5, 31, 32, 33, 63, 64, 65, 111, 112, 113, 127, 128, 129, 200, 500]))
    sig = k.sign(msg)
    out.append("SIG %s %s %s 1" % (pub.hex(), msg.hex() or "-", sig.hex()))
    # forgeries that must all be rejected
    out.append("SIG %s %s %s 0" % (pub.hex(), flip(msg, 0).hex() if msg else b"\x00".hex(), sig.hex()))
    out.append("SIG %s %s %s 0" % (pub.hex(), msg.hex() or "-", flip(sig, rng.randrange(64), rng.randrange(8)).hex()))
    other = pub_of(Ed25519PrivateKey.generate())
    out.append("SIG %s %s %s 0" % (other.hex(), msg.hex() or "-", sig.hex()))
    # S + L (non-canonical encoding of the same signature) must be rejected
    L = 2**252 + 27742317777372353535851937790883648493
    s_plus = (int.from_bytes(sig[32:], "little") + L)
    if s_plus < 2**256:
        out.append("SIG %s %s %s 0" % (pub.hex(), msg.hex() or "-", (sig[:32] + s_plus.to_bytes(32, "little")).hex()))

# not a curve point as public key
out.append("SIG %s %s %s 0" % ("02" + "00" * 31, "00", "00" * 64))
out.append("SIG %s %s %s 0" % ("ff" * 32, "00", "ff" * 64))

for n in [0, 1, 3, 55, 56, 57, 63, 64, 111, 112, 113, 119, 120, 127, 128, 129, 239, 240, 255, 256, 1000]:
    m = os.urandom(n)
    out.append("HASH %s %s" % (m.hex() or "-", hashlib.sha512(m).hexdigest()))
sys.stdout.write("\n".join(out) + "\n")
