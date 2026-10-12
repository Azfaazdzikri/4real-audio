"""Extract compact spectral features from the recorded Data Training folder.

    python training/extract_features.py "/content/drive/MyDrive/Data Training" out_dir

Layout: <guitar>/<pickup position>/<Sustain|Palm Mute|Strum>/[String N/]<note>.wav
Every file is converted to mono in memory (same rules as preprocess_wav.py: stereo -> louder
channel or average, leading silence trimmed, takes cut per technique), then described by:
  bands_attack_db  64 log-spaced bands, first ~60 ms after the note starts
  bands_body_db    64 log-spaced bands, rest of the note (Welch average)
  env_db           RMS envelope, 60 frames of 50 ms (first 3 s)
  rms_db, peak_db, duration, string, midi, fret
Writes out_dir/features.npz and out_dir/meta.json (a few MB for ~7000 files). Needs numpy only.

Note names follow the recording labels: string 6 open = "E1" (one octave below scientific
pitch notation), 5 = A1, 4 = D2, 3 = G2, 2 = B2, 1 = E3. fret = note - open string.
"""
import argparse
import json
import re
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from preprocess_wav import read_wav, technique_limit, to_mono, trim, write_wav24  # noqa: E402

RATE = 48000
NUM_BANDS = 64
BAND_EDGES = np.geomspace(70.0, 19000.0, NUM_BANDS + 1)
BAND_CENTERS = np.sqrt(BAND_EDGES[:-1] * BAND_EDGES[1:])
ATTACK_SAMPLES = int(0.060 * RATE)
BODY_START = int(0.060 * RATE)
ENV_FRAMES = 60
ENV_HOP = int(0.050 * RATE)
NPERSEG = 8192

OPEN_MIDI = {6: 40, 5: 45, 4: 50, 3: 55, 2: 59, 1: 64}  # sounding pitch of the open strings
PITCH_CLASS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
NOTE_RE = re.compile(r"^([A-Ga-g])([#b]?)(-?\d+)$")


def parse_note(stem: str):
    """'A#3' -> midi (labels are one octave below scientific pitch notation) or None."""
    m = NOTE_RE.match(stem.strip())
    if not m:
        return None
    pc = PITCH_CLASS[m.group(1).upper()] + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0)
    return 12 * (int(m.group(3)) + 2) + pc


def db(x, floor=1e-12):
    return 10.0 * np.log10(np.maximum(x, floor))


def band_levels(freqs, psd):
    out = np.empty(NUM_BANDS)
    for i in range(NUM_BANDS):
        lo, hi = BAND_EDGES[i], BAND_EDGES[i + 1]
        sel = (freqs >= lo) & (freqs < hi)
        out[i] = psd[sel].mean() if sel.any() else np.interp(BAND_CENTERS[i], freqs, psd)
    return db(out)


def welch_psd(x):
    if len(x) < NPERSEG:
        x = np.pad(x, (0, NPERSEG - len(x)))
    window = np.hanning(NPERSEG)
    hop = NPERSEG // 2
    acc = np.zeros(NPERSEG // 2 + 1)
    n = 0
    for start in range(0, len(x) - NPERSEG + 1, hop):
        seg = x[start:start + NPERSEG] * window
        acc += np.abs(np.fft.rfft(seg)) ** 2
        n += 1
    return np.fft.rfftfreq(NPERSEG, 1.0 / RATE), acc / (n * np.sum(window ** 2))


def describe(x):
    x = x - np.mean(x)
    n = len(x)
    seg = x[:ATTACK_SAMPLES]
    seg = np.pad(seg, (0, ATTACK_SAMPLES - len(seg))) * np.hanning(ATTACK_SAMPLES)
    attack = np.abs(np.fft.rfft(seg, 4096)) ** 2 / np.sum(np.hanning(ATTACK_SAMPLES) ** 2)
    f_attack = np.fft.rfftfreq(4096, 1.0 / RATE)

    body = x[BODY_START:] if n > BODY_START + NPERSEG // 2 else x
    f_body, psd = welch_psd(body)

    env = np.full(ENV_FRAMES, -120.0)
    for i in range(ENV_FRAMES):
        frame = x[i * ENV_HOP:(i + 1) * ENV_HOP]
        if len(frame) == 0:
            break
        env[i] = 10.0 * np.log10(max(float(np.mean(frame ** 2)), 1e-12))

    return {
        "bands_attack_db": band_levels(f_attack, attack),
        "bands_body_db": band_levels(f_body, psd),
        "env_db": env,
        "rms_db": 10.0 * np.log10(max(float(np.mean(body ** 2)), 1e-12)),
        "peak_db": 20.0 * np.log10(max(float(np.max(np.abs(x))), 1e-12)),
        "duration": n / RATE,
    }


def process(path: Path, root: Path, mono_out):
    rel = path.relative_to(root)
    parts = rel.parts
    meta = {"relpath": rel.as_posix(), "guitar": parts[0] if len(parts) > 1 else "",
            "position": parts[1] if len(parts) > 2 else "", "technique": parts[2] if len(parts) > 3 else "",
            "name": path.stem, "string": 0, "midi": -1, "fret": -1, "note": ""}
    for p in parts[3:-1]:
        m = re.match(r"(?i)string\s*(\d)", p)
        if m:
            meta["string"] = int(m.group(1))
    midi = parse_note(path.stem)
    if midi is not None:
        meta["midi"] = midi
        if meta["string"] in OPEN_MIDI:
            meta["fret"] = midi - OPEN_MIDI[meta["string"]]

    x, rate = read_wav(path)
    if rate != RATE:
        raise ValueError(f"{rate} Hz (expected {RATE})")
    mono, note = to_mono(x)
    meta["note"] = note
    y = trim(mono, rate, technique_limit(path, root))
    if mono_out is not None:
        write_wav24(mono_out / rel, y, rate)
    return meta, describe(y)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--mono-out", type=Path, default=None, help="also write the trimmed mono WAV copies here")
    ap.add_argument("--workers", type=int, default=6)
    args = ap.parse_args()

    if not args.input.is_dir():
        print(f"Input folder not found: {args.input}")
        return 1
    files = sorted({*args.input.rglob("*.wav"), *args.input.rglob("*.WAV")})
    print(f"{len(files)} wav files")

    metas, feats, errors = [], [], []

    def job(p):
        try:
            return process(p, args.input, args.mono_out)
        except Exception as e:  # noqa: BLE001 - report and continue
            return f"{p.relative_to(args.input)}: {e}"

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for i, res in enumerate(pool.map(job, files), 1):
            if isinstance(res, str):
                errors.append(res)
            else:
                metas.append(res[0])
                feats.append(res[1])
            if i % 500 == 0:
                print(f"  {i}/{len(files)}")

    if not feats:
        print("No files could be processed.")
        return 1
    args.out_dir.mkdir(parents=True, exist_ok=True)
    arrays = {k: np.array([f[k] for f in feats], dtype=np.float32)
              for k in ("bands_attack_db", "bands_body_db", "env_db", "rms_db", "peak_db", "duration")}
    arrays["string"] = np.array([m["string"] for m in metas], dtype=np.int16)
    arrays["midi"] = np.array([m["midi"] for m in metas], dtype=np.int16)
    arrays["fret"] = np.array([m["fret"] for m in metas], dtype=np.int16)
    arrays["band_centers_hz"] = BAND_CENTERS.astype(np.float32)
    np.savez_compressed(args.out_dir / "features.npz", **arrays)
    (args.out_dir / "meta.json").write_text(json.dumps(metas, indent=0), encoding="utf-8")

    unparsed = [m["relpath"] for m in metas if m["technique"] != "Strum" and m["fret"] < 0]
    out_of_range = [m["relpath"] for m in metas if m["fret"] > 12]
    print(f"\nprocessed {len(metas)} files, {len(errors)} errors")
    print(f"notes without a readable string/fret: {len(unparsed)}; fret > 12: {len(out_of_range)}")
    for line in (errors + unparsed + out_of_range)[:30]:
        print("  -", line)
    size = (args.out_dir / "features.npz").stat().st_size / 1e6
    print(f"features.npz {size:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
