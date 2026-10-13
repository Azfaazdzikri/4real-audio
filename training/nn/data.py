"""Note-matched pair sampling for guitar-to-guitar training. numpy only (no torch)."""
import json
import zlib
from collections import defaultdict
from pathlib import Path

import numpy as np


def is_test_key(key: str, percent: int = 20) -> bool:
    """Deterministic split by note/chord identity: the same string/fret or chord is held out in every cell."""
    return zlib.crc32(key.encode("utf-8")) % 100 < percent


class PairData:
    def __init__(self, data_dir, hold_out_cells=()):
        data_dir = Path(data_dir)
        meta = json.loads((data_dir / "index.json").read_text(encoding="utf-8"))
        self.audio = np.load(data_dir / "audio.npy")
        self.items = meta["items"]
        self.cells = meta["cells"]
        self.cell_id = {c: i for i, c in enumerate(self.cells)}
        self.length = meta["length"]
        self.rate = meta["rate"]
        held = {self.cell_id[c] for c in self.cells if any(h in c for h in hold_out_cells)}
        self.held_out_cells = sorted(held)

        self.rows = defaultdict(dict)  # key -> {cell id -> row}
        for row, it in enumerate(self.items):
            if not it["skip"]:
                self.rows[it["key"]][self.cell_id[it["cell"]]] = row
        self.train_keys = [k for k, v in self.rows.items() if len(self._usable(v)) >= 2 and not is_test_key(k)]
        self.test_keys = [k for k, v in self.rows.items() if len(self._usable(v)) >= 2 and is_test_key(k)]

    def _usable(self, by_cell):
        return {c: r for c, r in by_cell.items() if c not in self.held_out_cells}

    def sample(self, batch, clip, rng, max_offset=24000, same_prob=0.05):
        """Returns x (B, clip), y (B, clip), source cell ids, target cell ids. Pairs share onset alignment."""
        xs, ys, src, tgt = [], [], [], []
        for _ in range(batch):
            key = self.train_keys[rng.integers(len(self.train_keys))]
            by_cell = self._usable(self.rows[key])
            cells = list(by_cell)
            a = cells[rng.integers(len(cells))]
            others = [c for c in cells if c != a]
            b = a if (not others or rng.random() < same_prob) else others[rng.integers(len(others))]
            off = int(rng.integers(0, min(max_offset, self.length - clip) + 1))
            xs.append(self.audio[by_cell[a], off:off + clip])
            ys.append(self.audio[by_cell[b], off:off + clip])
            src.append(a)
            tgt.append(b)
        return (np.stack(xs).astype(np.float32), np.stack(ys).astype(np.float32),
                np.array(src, dtype=np.int64), np.array(tgt, dtype=np.int64))

    def test_pairs(self, rng, per_key=12):
        """(key, source row, source cell, target row, target cell) for held-out notes/chords, cross-cell only."""
        out = []
        for key in self.test_keys:
            by_cell = self._usable(self.rows[key])
            cells = list(by_cell)
            pairs = [(a, b) for a in cells for b in cells if a != b]
            rng.shuffle(pairs)
            for a, b in pairs[:per_key]:
                out.append((key, by_cell[a], a, by_cell[b], b))
        return out
