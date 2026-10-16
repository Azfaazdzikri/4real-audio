"""Build the v3 training set: per-frame band spectra of every wav (mono, full length).

    python training/nn/prepare_frames.py "/content/drive/MyDrive/Data Training" /content/nn_frames

Per wav: stereo -> mono (same rules as preprocess_wav.py), leading silence trimmed, kept up to 8 s (Sustain, Strum)
or 4 s (Palm Mute). The audio itself is NOT stored: only the 64-band spectrum of each 4096-sample frame
(hop 512), a few hundred MB in total. Each guitar+pickup cell is level-normalised so the median RMS of its
sustained notes is -30 dB (added to the band values in dB). Also reports how different the left and right channels are.
Writes out_dir/frames.npy (float16, N x frames x 64 dB) and out_dir/index.json. numpy only, CPU only.
"""
import argparse
import json
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
from preprocess_wav import read_wav, to_mono, trim  # noqa: E402
from spec_common import HOP, N_BANDS, NFFT, PAD_DB, RATE, frame_bands_db, num_frames  # noqa: E402

LIMITS = {"sustain": 8.0, "strum": 8.0, "palm mute": 4.0}
TARGET_RMS_DB = -30.0
CLIP_PEAK_DB = -0.5
MIN_SECONDS = 0.5


def limit_for(parts):
    for p in parts[:-1]:
        if p.lower() in LIMITS:
            return LIMITS[p.lower()]
    return 8.0


def load(path: Path, parts):
    x, rate = read_wav(path)
    if rate != RATE:
        raise ValueError(f"{rate} Hz (expected {RATE})")
    lr_db = None
    if x.shape[1] == 2:
        side = float(np.mean((x[:, 0] - x[:, 1]) ** 2))
        mid = float(np.mean((0.5 * (x[:, 0] + x[:, 1])) ** 2))
        lr_db = 10.0 * np.log10(max(side, 1e-20) / max(mid, 1e-20))
    mono, _ = to_mono(x)
    y = trim(mono, rate, limit_for(parts)).astype(np.float32)
    peak_db = 20.0 * np.log10(max(float(np.max(np.abs(y))), 1e-12)) if len(y) else -120.0
    a, b = int(0.1 * RATE), int(1.1 * RATE)
    rms_db = 10.0 * np.log10(max(float(np.mean(y[a:b] ** 2)), 1e-12)) if len(y) > b else None
    return frame_bands_db(y), len(y), peak_db, rms_db, lr_db


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--workers", type=int, default=6)
    args = ap.parse_args()

    files = sorted({*args.input.rglob("*.wav"), *args.input.rglob("*.WAV")})
    print(f"{len(files)} wav files", flush=True)
    items, frames, errors, lr_stats = [], [], [], []

    def job(p):
        try:
            return p, load(p, p.relative_to(args.input).parts)
        except Exception as e:  # noqa: BLE001
            return p, str(e)

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for i, (p, res) in enumerate(pool.map(job, files), 1):
            rel = p.relative_to(args.input).parts
            if isinstance(res, str):
                errors.append(f"{'/'.join(rel)}: {res}")
                continue
            fr, n, peak_db, rms_db, lr_db = res
            items.append({"cell": f"{rel[0]}|{rel[1]}", "key": "/".join(rel[2:]), "technique": rel[2], "n": int(n),
                          "nf": int(fr.shape[0]), "peak_db": round(float(peak_db), 2),
                          "rms_db": None if rms_db is None else round(float(rms_db), 2)})
            frames.append(fr)
            if lr_db is not None:
                lr_stats.append(lr_db)
            if i % 250 == 0:
                print(f"  {i}/{len(files)}", flush=True)

    fmax = max(f.shape[0] for f in frames)
    cells = sorted({it["cell"] for it in items})
    gain_db = {}
    for c in cells:
        levels = [it["rms_db"] for it in items if it["cell"] == c and it["technique"] == "Sustain" and it["rms_db"] is not None]
        gain_db[c] = TARGET_RMS_DB - float(np.median(levels)) if levels else 0.0

    out = np.full((len(items), fmax, N_BANDS), PAD_DB, dtype=np.float16)
    for i, (it, fr) in enumerate(zip(items, frames)):
        it["gain_db"] = round(gain_db[it["cell"]], 2)
        it["skip"] = bool(it["peak_db"] >= CLIP_PEAK_DB or it["n"] < MIN_SECONDS * RATE or it["nf"] < 8)
        out[i, :fr.shape[0]] = (fr + gain_db[it["cell"]]).astype(np.float16)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    np.save(args.out_dir / "frames.npy", out)
    (args.out_dir / "index.json").write_text(json.dumps({"rate": RATE, "nfft": NFFT, "hop": HOP, "fmax": fmax,
                                                         "cells": cells, "items": items}), encoding="utf-8")
    print(f"\n{len(items)} clips, {len(cells)} cells, {sum(it['skip'] for it in items)} skipped, {len(errors)} errors")
    print(f"frames.npy {out.nbytes / 1e9:.2f} GB, up to {fmax} frames ({fmax * HOP / RATE:.1f} s) per clip")
    if lr_stats:
        a = np.array(lr_stats)
        print(f"stereo files: side/mid energy ratio (dB, very negative = both channels identical): "
              f"median {np.median(a):.1f}, 10% {np.percentile(a, 10):.1f}, 90% {np.percentile(a, 90):.1f}")
    for c in cells:
        print(f"  gain {gain_db[c]:+6.1f} dB  {c}")
    for line in errors[:20]:
        print("  -", line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
