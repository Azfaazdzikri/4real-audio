"""Convert the recorded Data Training folder to mono 24-bit 48 kHz and trim long takes.

    python training/preprocess_wav.py "Data Train" "Data Mono"
    python training/preprocess_wav.py "Data Train" "Data Mono" --dry-run

- The folder structure is mirrored into the output folder; the input is never modified.
- Stereo -> mono. If one channel is much quieter (a Hi-Z input recorded on only the left or right
  channel), the louder channel is used so the level is NOT halved; otherwise the channels are averaged.
- Leading silence is trimmed (keeps 20 ms before the first note), and takes are cut to a maximum
  length per technique with a short fade-out. Levels are never normalised (the level difference
  between guitars is part of what the model learns).
- Needs numpy only. Reads 16/24/32-bit PCM WAV; 32-bit float WAV and other sample rates are reported and skipped.
"""
import argparse
import sys
import wave
from pathlib import Path

import numpy as np

MAX_SECONDS = {"sustain": 8.0, "palm mute": 3.0, "strum": 5.0}
DEFAULT_MAX_SECONDS = 8.0
ONSET_DB = -60.0       # first sample above this level (dBFS) counts as the note start
PRE_ROLL_S = 0.02
FADE_S = 0.05
QUIET_CHANNEL_DB = -40.0


def read_wav(path: Path):
    """Returns (samples float64 shape (n, channels) in -1..1, sample_rate)."""
    with wave.open(str(path), "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        x = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
        x = np.where(x & 0x800000, x - 0x1000000, x).astype(np.float64) / 8388608.0
    elif width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 4:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"unsupported sample width {width}")
    return x.reshape(-1, ch), rate


def write_wav24(path: Path, x: np.ndarray, rate: int):
    q = np.clip(np.round(x * 8388607.0), -8388608, 8388607).astype(np.int32)
    q = np.where(q < 0, q + 0x1000000, q).astype(np.uint32)
    b = np.stack([q & 0xFF, (q >> 8) & 0xFF, (q >> 16) & 0xFF], axis=1).astype(np.uint8)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(3)
        w.setframerate(rate)
        w.writeframes(b.tobytes())


def db(x: float) -> float:
    return 20.0 * np.log10(max(x, 1e-12))


def to_mono(x: np.ndarray):
    """Returns (mono, note)."""
    if x.shape[1] == 1:
        return x[:, 0], ""
    left, right = x[:, 0], x[:, 1]
    rl, rr = float(np.sqrt(np.mean(left ** 2))), float(np.sqrt(np.mean(right ** 2)))
    if db(rr) < db(rl) + QUIET_CHANNEL_DB:
        return left, "used left channel"
    if db(rl) < db(rr) + QUIET_CHANNEL_DB:
        return right, "used right channel"
    return 0.5 * (left + right), ""


def trim(x: np.ndarray, rate: int, max_seconds: float):
    thresh = 10.0 ** (ONSET_DB / 20.0)
    above = np.flatnonzero(np.abs(x) > thresh)
    start = max(0, int(above[0]) - int(PRE_ROLL_S * rate)) if above.size else 0
    y = x[start:]
    limit = int(max_seconds * rate)
    if len(y) > limit:
        y = y[:limit].copy()
        fade = int(FADE_S * rate)
        y[-fade:] *= np.linspace(1.0, 0.0, fade)
    return y


def technique_limit(path: Path, root: Path):
    for part in path.relative_to(root).parts[:-1]:
        if part.lower() in MAX_SECONDS:
            return MAX_SECONDS[part.lower()]
    return DEFAULT_MAX_SECONDS


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--dry-run", action="store_true", help="only report, write nothing")
    args = ap.parse_args()

    if not args.input.is_dir():
        print(f"Input folder not found: {args.input}")
        return 1
    if args.output.resolve() == args.input.resolve():
        print("Output folder must be different from the input folder.")
        return 1

    files = sorted(args.input.rglob("*.wav")) + sorted(args.input.rglob("*.WAV"))
    done = skipped = 0
    bytes_in = bytes_out = 0
    notes = []
    for i, f in enumerate(files, 1):
        rel = f.relative_to(args.input)
        try:
            x, rate = read_wav(f)
        except Exception as e:  # noqa: BLE001 - report and continue
            notes.append(f"SKIP {rel}: {e}")
            skipped += 1
            continue
        if rate != 48000:
            notes.append(f"SKIP {rel}: {rate} Hz (expected 48000)")
            skipped += 1
            continue
        mono, note = to_mono(x)
        if note:
            notes.append(f"{rel}: {note}")
        y = trim(mono, rate, technique_limit(f, args.input))
        bytes_in += f.stat().st_size
        bytes_out += 44 + 3 * len(y)
        if not args.dry_run:
            write_wav24(args.output / rel, y, rate)
        done += 1
        if i % 200 == 0:
            print(f"  {i}/{len(files)} files")

    print(f"\n{done} files {'checked' if args.dry_run else 'converted'}, {skipped} skipped")
    print(f"size: {bytes_in / 1e9:.2f} GB -> {bytes_out / 1e9:.2f} GB")
    for line in notes[:40]:
        print("  -", line)
    if len(notes) > 40:
        print(f"  ... and {len(notes) - 40} more notes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
