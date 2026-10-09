"""Offline prototype: make a DI recording of one guitar sound like another, using the
physical guitar-electronics model (no neural network yet).

Example (Strat HSS bridge humbucker -> Les Paul bridge pickup, knobs wide open):

    python training/process_wav.py my_di.wav out.wav \
        --source "strat-hss:position=1" \
        --target "lp:selector=bridge,vol_bridge=10,tone_bridge=8"

Input must be a dry DI recording (no amp, no effects), WAV 16/32-bit PCM or 32-bit float.
Knob values are 0..10. Run with --help for more.
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from guitar_circuit import GUITARS, Rig, parse_spec  # noqa: E402
from transfer import apply_fir, correction_db, design_min_phase_fir  # noqa: E402


def read_wav(path: str):
    fs, data = wavfile.read(path)
    if data.dtype == np.int16:
        x = data.astype(np.float64) / 32768.0
    elif data.dtype == np.int32:
        x = data.astype(np.float64) / 2147483648.0
    elif data.dtype in (np.float32, np.float64):
        x = data.astype(np.float64)
    else:
        raise ValueError(f"unsupported WAV sample type {data.dtype}; export 16-bit or 32-bit float")
    return fs, x


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--source", required=True,
                   help=f"guitar you played, e.g. 'strat-hss:position=1'. Guitars: {sorted(GUITARS)}")
    p.add_argument("--target", required=True,
                   help="guitar to sound like, e.g. 'lp:selector=bridge,vol_bridge=10,tone_bridge=8'")
    p.add_argument("--cable-pf", type=float, default=500.0, help="cable capacitance in pF (default 500)")
    p.add_argument("--max-boost-db", type=float, default=12.0)
    p.add_argument("--no-level", action="store_true",
                   help="do not apply the level difference between the two guitars")
    p.add_argument("--plot", metavar="PNG", help="save the response curves to a PNG")
    args = p.parse_args()

    fs, x = read_wav(args.input)
    rig = Rig(cable_pf=args.cable_pf)
    freqs = np.geomspace(20.0, min(20000.0, fs / 2 * 0.99), 600)

    src_guitar, src_controls = parse_spec(args.source)
    tgt_guitar, tgt_controls = parse_spec(args.target)
    src_resp = src_guitar.response(freqs, rig=rig, **src_controls)
    tgt_resp = tgt_guitar.response(freqs, rig=rig, **tgt_controls)
    corr = correction_db(src_resp, tgt_resp, max_boost_db=args.max_boost_db)
    if args.no_level:
        corr = corr - float(np.interp(np.log(1000.0), np.log(freqs), corr))

    h = design_min_phase_fir(freqs, corr, fs)
    y = apply_fir(x, h)

    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak > 1.0:
        print(f"warning: output peaks at {peak:.2f} (clips in fixed-point formats); written as 32-bit float")
    wavfile.write(args.output, fs, y.astype(np.float32))

    for f in (100, 500, 1000, 3000, 5000, 8000):
        print(f"{f:>5} Hz: {float(np.interp(np.log(f), np.log(freqs), corr)):+6.1f} dB")
    print(f"wrote {args.output}  (peak {peak:.2f})")

    if args.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        db = lambda h_: 20 * np.log10(np.maximum(np.abs(h_), 1e-12))
        plt.figure(figsize=(8, 4.5))
        plt.semilogx(freqs, db(src_resp), label=f"source: {args.source}")
        plt.semilogx(freqs, db(tgt_resp), label=f"target: {args.target}")
        plt.semilogx(freqs, corr, "k--", label="applied correction")
        plt.xlabel("Hz"); plt.ylabel("dB"); plt.grid(True, which="both", alpha=0.3); plt.legend()
        plt.tight_layout(); plt.savefig(args.plot, dpi=120)
        print(f"wrote {args.plot}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
