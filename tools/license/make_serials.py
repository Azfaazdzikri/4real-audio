#!/usr/bin/env python3
"""Makes serial numbers for the guitars and the SQL that registers them.

  python3 tools/license/make_serials.py --guitars LP,STR --count 5 --note "Batch Nov 2026" --out serials_batch1

Writes two files:
  <out>.csv  serial,guitars,note   - the PLAIN serials: this is what you give to buyers. Keep it private.
  <out>.sql  insert statements     - paste into the Supabase SQL editor. Only the SHA-256 of each serial goes into the database.
A serial is 20 random characters (100 bits) in 4 groups of 5, e.g. K7M4Q-2XB9D-RT6WH-P3NVC; it unlocks the listed guitars for
the first account that enters it. Several guitars in one serial make a bundle. --guitars ALL means every guitar.
"""
import argparse
import hashlib
import re
import secrets
import sys

ALL = ["LP", "335", "SG", "PRS", "RIC", "STR", "TCR", "FLC"]
ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  # no 0 O 1 I, so serials can be read aloud and typed without mistakes


def normalize(text):
    # must match normalizeSerial() in server/supabase/functions/_shared/core.ts
    return re.sub(r"[^A-Z0-9]", "", text.upper())


def new_serial():
    raw = "".join(secrets.choice(ALPHABET) for _ in range(20))
    return "-".join(raw[i:i + 5] for i in range(0, 20, 5))


def sql_text(s):
    return "'" + s.replace("'", "''") + "'"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--guitars", required=True, help="comma separated codes (LP,335,SG,PRS,RIC,STR,TCR,FLC) or ALL")
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--note", default="")
    ap.add_argument("--out", required=True, help="file name without extension")
    a = ap.parse_args()

    codes = ALL if a.guitars.strip().upper() == "ALL" else [c.strip().upper() for c in a.guitars.split(",") if c.strip()]
    bad = [c for c in codes if c not in ALL]
    if bad or not codes:
        sys.exit("unknown guitar code(s): %s (valid: %s)" % (", ".join(bad) or "none given", ", ".join(ALL)))
    if not 1 <= a.count <= 5000:
        sys.exit("--count must be 1..5000")

    serials = [new_serial() for _ in range(a.count)]
    array = "array[" + ",".join(sql_text(c) for c in codes) + "]::text[]"
    values = ",\n".join(
        "  (%s, %s, %s)" % (sql_text(hashlib.sha256(normalize(s).encode()).hexdigest()), array, sql_text(a.note)) for s in serials
    )
    with open(a.out + ".sql", "x") as f:
        f.write("insert into public.serials (serial_hash, guitar_codes, note) values\n%s;\n" % values)
    with open(a.out + ".csv", "x") as f:
        f.write("serial,guitars,note\n")
        for s in serials:
            f.write('%s,"%s","%s"\n' % (s, " ".join(codes), a.note.replace('"', "'")))
    print("%d serial(s) for %s written to %s.csv and %s.sql" % (a.count, ",".join(codes), a.out, a.out))


if __name__ == "__main__":
    main()
