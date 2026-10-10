"""Scan the Data Training folder and report what has been recorded.

    python training/scan_dataset.py "path/to/Data Training"

Layout: <guitar>/<pickup position>/<Sustain|Palm Mute|Strum>/*.wav
Reports files, total duration, size, and flags problems: stereo files (record mono), 
sample rate other than 48 kHz, bit depth other than 24, and very long sustain/palm-mute takes.
Only the standard library is used (no numpy needed).
"""
import sys
import wave
from pathlib import Path

TECHNIQUES = ("Sustain", "Palm Mute", "Strum")
MAX_SECONDS = {"Sustain": 10.0, "Palm Mute": 5.0, "Strum": 8.0}


def describe(path: Path):
    try:
        with wave.open(str(path), "rb") as w:
            return w.getnchannels(), w.getframerate(), w.getsampwidth() * 8, w.getnframes() / w.getframerate()
    except (wave.Error, EOFError):
        return None  # e.g. 32-bit float WAV or not a WAV


def main(root: Path) -> int:
    if not root.is_dir():
        print(f"Folder not found: {root}")
        return 1

    total_files = total_bytes = 0
    total_seconds = 0.0
    issues = []
    for guitar in sorted(p for p in root.iterdir() if p.is_dir()):
        print(f"\n{guitar.name}")
        for pos in sorted(p for p in guitar.iterdir() if p.is_dir()):
            cells = []
            for tech in TECHNIQUES:
                folder = pos / tech
                files = sorted(folder.glob("*.wav")) if folder.is_dir() else []
                seconds = 0.0
                for f in files:
                    info = describe(f)
                    total_bytes += f.stat().st_size
                    if info is None:
                        issues.append(f"{f.relative_to(root)}: cannot read (not PCM WAV?)")
                        continue
                    ch, sr, bits, dur = info
                    seconds += dur
                    if ch != 1:
                        issues.append(f"{f.relative_to(root)}: {ch} channels (record mono)")
                    if sr != 48000:
                        issues.append(f"{f.relative_to(root)}: {sr} Hz (expected 48000)")
                    if bits != 24:
                        issues.append(f"{f.relative_to(root)}: {bits}-bit (expected 24)")
                    if dur > MAX_SECONDS[tech]:
                        issues.append(f"{f.relative_to(root)}: {dur:.0f} s (trim to <= {MAX_SECONDS[tech]:.0f} s)")
                total_files += len(files)
                total_seconds += seconds
                cells.append(f"{tech}: {len(files)} files, {seconds:.0f} s")
            print(f"  {pos.name:<22} " + " | ".join(cells))

    print(f"\nTotal: {total_files} files, {total_seconds / 60:.1f} min, {total_bytes / 1e6:.0f} MB")
    if issues:
        print(f"\n{len(issues)} issue(s):")
        for line in issues[:50]:
            print("  -", line)
        if len(issues) > 50:
            print(f"  ... and {len(issues) - 50} more")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(Path(sys.argv[1])))
