#!/usr/bin/env python3
"""Writes tests/license/token_cases.txt: tokens in the exact format the licence server makes, signed with a THROWAWAY test key.
Run:  python3 -I tests/license/gen_token_cases.py > tests/license/token_cases.txt
"""
import base64, json, sys
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

NOW = 1_800_000_000
MID = "machine-aaaaaaaaaaaaaaaa"


def b64u(b):
    return base64.urlsafe_b64encode(b).rstrip(b"=").decode()


def raw_pub(k):
    return k.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def make(key, payload_obj=None, payload_bytes=None):
    payload = payload_bytes if payload_bytes is not None else json.dumps(payload_obj, separators=(",", ":")).encode()
    return b64u(payload) + "." + b64u(key.sign(payload))


def base(**kw):
    d = {"v": 1, "sub": "user-1", "email": "a@b.c", "dev": "dev-1", "mid": MID, "guitars": ["LP", "STR"],
         "iat": NOW - 100, "exp": NOW + 86400}
    d.update(kw)
    return d


server = Ed25519PrivateKey.generate()
other = Ed25519PrivateKey.generate()
out = ["KEY " + b64u(raw_pub(server)), "MID " + MID, "NOW %d" % NOW]


def case(name, expected, guitars, token):
    out.append("CASE %s %s %s %s" % (name, expected, guitars, token))


case("ok_two_guitars", "Ok", "LP,STR", make(server, base()))
case("ok_no_guitars", "Ok", "-", make(server, base(guitars=[])))
case("ok_all_eight", "Ok", "LP,335,SG,PRS,RIC,STR,TCR,FLC", make(server, base(guitars=["LP", "335", "SG", "PRS", "RIC", "STR", "TCR", "FLC"])))
case("ok_unicode_email", "Ok", "LP", make(server, base(email="aé中@b.c", guitars=["LP"])))
case("ok_small_clock_skew", "Ok", "LP,STR", make(server, base(iat=NOW + 3600)))
case("expired", "Expired", "-", make(server, base(exp=NOW - 1)))
case("expires_exactly_now", "Expired", "-", make(server, base(exp=NOW)))
case("issued_in_the_future", "NotYetValid", "-", make(server, base(iat=NOW + 3 * 86400)))
case("other_machine", "WrongMachine", "-", make(server, base(mid="machine-bbbbbbbbbbbbbbbb")))
case("signed_by_other_key", "BadSignature", "-", make(other, base()))
case("wrong_version", "WrongVersion", "-", make(server, base(v=2)))
case("missing_sub", "Malformed", "-", make(server, payload_bytes=json.dumps({k: v for k, v in base().items() if k != "sub"}).encode()))
case("guitars_not_array", "Malformed", "-", make(server, base(guitars="LP")))
case("guitar_not_string", "Malformed", "-", make(server, base(guitars=[1, 2])))
case("payload_not_object", "Malformed", "-", make(server, payload_bytes=b"[1,2,3]"))
case("payload_not_json", "Malformed", "-", make(server, payload_bytes=b"hello"))

good = make(server, base())
p, s = good.split(".")
# someone edits the licence list but keeps the old signature
edited = b64u(json.dumps(base(guitars=["LP", "335", "SG", "PRS", "RIC", "STR", "TCR", "FLC"]), separators=(",", ":")).encode())
case("tampered_payload", "BadSignature", "-", edited + "." + s)
case("tampered_signature", "BadSignature", "-", p + "." + b64u(bytes(64)))
case("no_dot", "Malformed", "-", p)
case("two_dots", "Malformed", "-", good + ".x")
case("empty_payload", "Malformed", "-", "." + s)
case("short_signature", "Malformed", "-", p + "." + b64u(bytes(10)))
case("garbage", "Malformed", "-", "!!!.???")
sys.stdout.write("\n".join(out) + "\n")
