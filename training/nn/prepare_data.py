"""Build the neural-network training set from the Data Training folder.

    python training/nn/prepare_data.py "/content/drive/MyDrive/Data Training" /content/nn_data

For every wav: stereo -> mono (same rules as preprocess_wav.py), leading silence trimmed, first
--seconds kept (zero padded). Each guitar+pickup position ("cell") is level-normalised so the median
RMS of its sustained notes is -30 dB (recording gain differs between sessions). Clipped files
(peak >= -0.5 dBFS) and very short files are flagged and skipped by training.
Writes out_dir/audio.npy (float16, N x samples) and out_dir/index.json. Needs numpy only.
"""
import argparse
import json
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from preprocess_wav import read_wav, technique_limit, to_mono, trim  # noqa: E402

RATE = 48000
TARGET_RMS_DB = -30.0
CLIP_PEAK_DB = -0.5
MIN_SECONDS = 0.5


def load(path: Path, root: Path, length: int):
    x, rate = read_wav(path)
    if rate != RATE:
        raise ValueError(f"{rate} Hz (expected {RATE})")
    mono, _ = to_mono(x)
    y = trim(mono, rate, technique_limit(path, root))
    out = np.zeros(length, dtype=np.float32)
    n = min(length, len(y))
    out[:n] = y[:n]
    peak_db = 20.0 * np.log10(max(float(np.max(np.abs(y))), 1e-12))
    return out, n, peak_db


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--seconds", type=float, default=2.0)
    ap.add_argument("--workers", type=int, default=6)
    args = ap.parse_args()

    length = int(args.seconds * RATE)
    files = sorted({*args.input.rglob("*.wav"), *args.input.rglob("*.WAV")})
    print(f"{len(files)} wav files")

    items, rows, errors = [], [], []

    def job(p):
        try:
            return p, load(p, args.input, length)
        except Exception as e:  # noqa: BLE001
            return p, str(e)

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for i, (p, res) in enumerate(pool.map(job, files), 1):
            rel = p.relative_to(args.input).parts
            if isinstance(res, str):
                errors.append(f"{'/'.join(rel)}: {res}")
                continue
            audio, n, peak_db = res
            items.append({"cell": f"{rel[0]}|{rel[1]}", "key": "/".join(rel[2:]), "technique": rel[2],
                          "n": int(n), "peak_db": round(float(peak_db), 2)})
            rows.append(audio)
            if i % 500 == 0:
                print(f"  {i}/{len(files)}")
    audio = np.stack(rows)

    # per-cell level normalisation from sustained notes (0.1 s - 1.1 s after the note starts)
    start, stop = int(0.1 * RATE), int(1.1 * RATE)
    cells = sorted({it["cell"] for it in items})
    gain_db = {}
    for c in cells:
        levels = [10 * np.log10(max(float(np.mean(audio[i, start:stop] ** 2)), 1e-12))
                  for i, it in enumerate(items) if it["cell"] == c and it["technique"] == "Sustain"
                  and it["n"] > stop]
        gain_db[c] = TARGET_RMS_DB - float(np.median(levels)) if levels else 0.0
    for i, it in enumerate(items):
        it["gain_db"] = round(gain_db[it["cell"]], 2)
        it["skip"] = bool(it["peak_db"] >= CLIP_PEAK_DB or it["n"] < MIN_SECONDS * RATE)
        audio[i] *= 10.0 ** (gain_db[it["cell"]] / 20.0)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    np.save(args.out_dir / "audio.npy", audio.astype(np.float16))
    (args.out_dir / "index.json").write_text(json.dumps({"rate": RATE, "length": length, "cells": cells,
                                                         "items": items}), encoding="utf-8")
    skipped = sum(it["skip"] for it in items)
    print(f"\n{len(items)} clips, {len(cells)} cells, {skipped} skipped (clipped or too short), {len(errors)} errors")
    for c in cells:
        print(f"  gain {gain_db[c]:+6.1f} dB  {c}")
    for line in errors[:20]:
        print("  -", line)
    print(f"audio.npy {audio.nbytes / 2e9:.2f} GB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
