"""Train the spectral-envelope model (v3): a small causal network that predicts, for every analysis frame, a gain
curve (64 bands, dB) that turns one guitar's spectrum into another's. At run time the gain curve is applied as a
minimum-phase FIR filter, so the output is the input FILTERED: no added distortion, noise or crackle.

    python training/nn/train_spec.py /content/nn_frames /content/spec_run --steps 6000

Data   : prepare_frames.py output (frames.npy, index.json). No waveforms are needed, so it runs on a CPU too.
Pairs  : the same note/chord recorded on two different cells (guitar + pickup position), not sample aligned.
Loss   : the evaluation metric itself (shape-only RMS over the 64 bands) on the whole clip, plus the same on
         170 ms segments (so decay / palm-mute behaviour is learned), plus small smoothness penalties.
Model  : per-pair static EQ (starts at the measured median) + a dynamic correction predicted from the last ~0.3 s of
         frame spectra of the INPUT (causal, conditioned on source/target cell with FiLM), zero-mean per frame.
Test   : notes/chords held out of training in every cell; compares "no change", the static EQ and the model.
"""
import argparse
import json
import math
import os
import sys
import time
import warnings
from collections import defaultdict
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from data import is_test_key  # noqa: E402
from metrics import diff, rms_shape  # noqa: E402
from spec_common import N_BANDS, PAD_DB, RANGE, clip_db  # noqa: E402
from spec_metrics import FREQ_OK, MARGIN_DB, diff_wide, fill_nan, rms_curve, rms_wide  # noqa: E402

EQ_LIMIT_DB = 15.0
MAX_DELTA_DB = 8.0
N_CTRL = 20       # the dynamic correction is a smooth curve through 20 control points (linear in log frequency)
LAG = 4           # frames: the gain used for frame f comes from frames <= f - LAG (analysis -> filter delay)
SEG = 16          # frames per loss segment (16 x 10.7 ms = 170 ms)
FEATURES = N_BANDS + 1


class Block(nn.Module):
    def __init__(self, ch, dilation, cond_dim, kernel=3):
        super().__init__()
        self.pad = (kernel - 1) * dilation
        self.conv = nn.Conv1d(ch, 2 * ch, kernel, dilation=dilation)
        self.film = nn.Linear(cond_dim, 4 * ch)
        self.res = nn.Conv1d(ch, ch, 1)
        self.skip = nn.Conv1d(ch, ch, 1)

    def forward(self, x, cond):
        h = self.conv(F.pad(x, (self.pad, 0)))
        gamma, beta = self.film(cond).unsqueeze(-1).chunk(2, dim=1)
        h = h * (1.0 + gamma) + beta
        a, b = h.chunk(2, dim=1)
        z = torch.tanh(a) * torch.sigmoid(b)
        return x + self.res(z), self.skip(z)


class SpecEQ(nn.Module):
    def __init__(self, n_cells, ch=32, dilations=(1, 2, 4, 8), eq_init=None, emb=16, cond=32):
        super().__init__()
        self.n_cells, self.ch, self.dilations = n_cells, ch, tuple(dilations)
        self.emb = nn.Embedding(n_cells, emb)
        self.cond = nn.Sequential(nn.Linear(2 * emb, cond), nn.SiLU())
        self.inp = nn.Conv1d(FEATURES, ch, 1)
        self.blocks = nn.ModuleList([Block(ch, d, cond) for d in dilations])
        self.head = nn.Sequential(nn.SiLU(), nn.Conv1d(ch, N_CTRL, 1))
        nn.init.zeros_(self.head[1].weight)
        nn.init.zeros_(self.head[1].bias)
        self.eq = nn.Parameter(torch.zeros(n_cells, n_cells, N_BANDS) if eq_init is None else torch.from_numpy(eq_init).float())
        self.register_buffer("range_mask", torch.from_numpy(RANGE.astype(np.float32)))
        self.register_buffer("freq_mask", torch.from_numpy(FREQ_OK))
        w = np.zeros((N_CTRL, N_BANDS), dtype=np.float32)
        for j in range(N_BANDS):
            p = j * (N_CTRL - 1) / (N_BANDS - 1)
            i0 = min(int(math.floor(p)), N_CTRL - 2)
            w[i0, j], w[i0 + 1, j] = 1.0 - (p - i0), p - i0
        self.register_buffer("ctrl_w", torch.from_numpy(w))

    def zero_mean(self, g):
        m = self.range_mask
        return g - (g * m).sum(-1, keepdim=True) / m.sum()

    def eq_curve_db(self, src, tgt):
        return self.zero_mean(self.eq[src, tgt].clamp(-EQ_LIMIT_DB, EQ_LIMIT_DB))

    def features(self, L):
        """L: (B, F, 64) band dB of the INPUT -> (B, 65, F): shape (mean removed) and a level cue."""
        shape = (self.zero_mean(L)).clamp(-60.0, 30.0) / 20.0
        level = 10.0 * torch.log10((10.0 ** (L / 10.0)).sum(-1, keepdim=True) + 1e-14)
        level = ((level + 30.0) / 30.0).clamp(-3.0, 2.0)
        return torch.cat([shape, level], dim=-1).transpose(1, 2)

    def delta_raw(self, L, src, tgt):
        """Dynamic correction computed from frames <= f (no lag yet): (B, F, 64) dB, zero-mean per frame."""
        c = self.cond(torch.cat([self.emb(src), self.emb(tgt)], dim=-1))
        h = self.inp(self.features(L))
        skip = 0
        for blk in self.blocks:
            h, s = blk(h, c)
            skip = skip + s
        out = self.head(skip)                                      # (B, N_CTRL, F)
        out = MAX_DELTA_DB * torch.tanh(out / MAX_DELTA_DB)
        return self.zero_mean(out.transpose(1, 2) @ self.ctrl_w)  # (B, F, 64)

    def gains(self, L, src, tgt, lag=LAG):
        """Total gain per frame (static + dynamic), (B, F, 64) dB, and the dynamic part."""
        d = self.delta_raw(L, src, tgt)
        if lag:
            d = F.pad(d, (0, 0, lag, 0))[:, :L.shape[1]]           # d[f] <- d[f - lag]
        return self.eq_curve_db(src, tgt).unsqueeze(1) + d, d


# ----------------------------------------------------------------------------------------------- data

class FrameData:
    def __init__(self, data_dir, hold_out_cells=()):
        data_dir = Path(data_dir)
        meta = json.loads((data_dir / "index.json").read_text(encoding="utf-8"))
        self.frames = np.load(data_dir / "frames.npy")
        self.items, self.cells = meta["items"], meta["cells"]
        self.cell_id = {c: i for i, c in enumerate(self.cells)}
        self.nf = np.array([it["nf"] for it in self.items])
        held = {self.cell_id[c] for c in self.cells if any(h in c for h in hold_out_cells)}
        self.held_out_cells = sorted(held)
        self.rows = defaultdict(dict)
        for row, it in enumerate(self.items):
            if not it["skip"]:
                self.rows[it["key"]][self.cell_id[it["cell"]]] = row
        self.train_keys = [k for k, v in self.rows.items() if len(self._usable(v)) >= 2 and not is_test_key(k)]
        self.test_keys = [k for k, v in self.rows.items() if len(self._usable(v)) >= 2 and is_test_key(k)]
        self.clip = np.zeros((len(self.items), N_BANDS))
        for i, it in enumerate(self.items):
            if not it["skip"]:
                self.clip[i] = clip_db(self.frames[i, :it["nf"]].astype(np.float32))
        # recording noise floor of every cell and band: the 1st percentile of all its frames (decay tails)
        self.floor = np.zeros((len(self.cells), N_BANDS), dtype=np.float32)
        for c in range(len(self.cells)):
            rows = [i for i, it in enumerate(self.items) if self.cell_id[it["cell"]] == c and not it["skip"]]
            sel = np.concatenate([self.frames[i, :self.nf[i]].astype(np.float32) for i in rows], axis=0)
            self.floor[c] = np.percentile(sel, 1.0, axis=0)

    def _usable(self, by_cell):
        return {c: r for c, r in by_cell.items() if c not in self.held_out_cells}

    def sample(self, batch, rng, same_prob=0.05):
        rows_s, rows_t, src, tgt = [], [], [], []
        for _ in range(batch):
            key = self.train_keys[rng.integers(len(self.train_keys))]
            by_cell = self._usable(self.rows[key])
            cells = list(by_cell)
            a = cells[rng.integers(len(cells))]
            others = [c for c in cells if c != a]
            b = a if (not others or rng.random() < same_prob) else others[rng.integers(len(others))]
            rows_s.append(by_cell[a]); rows_t.append(by_cell[b]); src.append(a); tgt.append(b)
        n = np.minimum(self.nf[rows_s], self.nf[rows_t])
        fmax = int(n.max())
        x = self.frames[rows_s, :fmax].astype(np.float32)
        y = self.frames[rows_t, :fmax].astype(np.float32)
        valid = (np.arange(fmax)[None, :] < n[:, None]).astype(np.float32)
        return x, y, valid, np.array(src), np.array(tgt), self.floor[src], self.floor[tgt]

    def test_pairs(self, rng, per_key=6):
        out = []
        for key in self.test_keys:
            by_cell = self._usable(self.rows[key])
            cells = list(by_cell)
            pairs = [(a, b) for a in cells for b in cells if a != b]
            rng.shuffle(pairs)
            for a, b in pairs[:per_key]:
                out.append((key, by_cell[a], a, by_cell[b], b))
        return out


# ----------------------------------------------------------------------------------------------- loss

def pooled_db(Ldb, w):
    """Weighted mean power over frames (axis -2), 3-band smoothing, back to dB. Ldb (..., T, 64), w (..., T)."""
    p = (10.0 ** (Ldb / 10.0) * w.unsqueeze(-1)).sum(-2) / w.sum(-1, keepdim=True).clamp(min=1.0)
    shp = p.shape
    q = F.avg_pool1d(F.pad(p.reshape(-1, 1, N_BANDS), (1, 1), mode="replicate"), 3, 1).reshape(shp)
    return 10.0 * torch.log10(q + 1e-14)


def shape_rms(pred, tgt, ok):
    """RMS (dB) of pred - tgt over the bands in the 0/1 mask `ok`, after removing the mean level."""
    n = ok.sum(-1).clamp(min=1.0)
    d = (pred - tgt) * ok
    d = (d - d.sum(-1, keepdim=True) / n.unsqueeze(-1)) * ok
    return torch.sqrt((d ** 2).sum(-1) / n + 1e-6)


def band_ok(src_db, tgt_db, fs, ft, fmask):
    """Bands that are clearly above both recordings' noise floors (see spec_metrics.py)."""
    return ((src_db > fs + MARGIN_DB) & (tgt_db > ft + MARGIN_DB) & fmask).float()


def spec_loss(Ls, Lt, valid, gains, delta, fs, ft, fmask):
    Lp = Ls + gains
    sc, tc = pooled_db(Ls, valid), pooled_db(Lt, valid)
    clip = shape_rms(pooled_db(Lp, valid), tc, band_ok(sc, tc, fs, ft, fmask)).mean()
    B, Fn, _ = Ls.shape
    pad = (-Fn) % SEG

    def seg(x):
        return F.pad(x, (0, 0, 0, pad), value=PAD_DB).reshape(B, -1, SEG, N_BANDS)
    w = F.pad(valid, (0, pad)).reshape(B, -1, SEG)
    ss, ts = pooled_db(seg(Ls), w), pooled_db(seg(Lt), w)
    ok = band_ok(ss, ts, fs.unsqueeze(1), ft.unsqueeze(1), fmask)             # (B, nseg, 64)
    r = shape_rms(pooled_db(seg(Lp), w), ts, ok)                               # (B, nseg)
    use = ((w.sum(-1) >= SEG // 2) & (ok.sum(-1) >= 15)).float()
    seg_loss = (r * use).sum() / use.sum().clamp(min=1.0)
    vm = valid.unsqueeze(-1)
    reg = 1e-3 * ((delta ** 2) * vm).sum() / (vm.sum() * N_BANDS) \
        + 1e-2 * (((delta[:, 1:] - delta[:, :-1]) ** 2) * vm[:, 1:]).sum() / (vm[:, 1:].sum() * N_BANDS)
    return clip + 0.5 * seg_loss + reg, clip.item(), seg_loss.item()


# ----------------------------------------------------------------------------------------------- evaluation

class Evaluator:
    def __init__(self, data: FrameData, device, rng):
        self.d, self.device = data, device
        self.pairs = data.test_pairs(rng)
        self.eq_cache = {}

    def static_eq(self, a, b, tech=None):
        """Median (tgt - src) band difference over the TRAINING notes, NaN where never measurable."""
        if (a, b, tech) not in self.eq_cache:
            ds = []
            for key in self.d.train_keys:
                if tech is not None and key.split("/")[0] != tech:
                    continue
                by_cell = self.d._usable(self.d.rows[key])
                if a in by_cell and b in by_cell:
                    ds.append(diff_wide(self.d.clip[by_cell[a]], self.d.clip[by_cell[b]], self.d.floor[a], self.d.floor[b]))
            self.eq_cache[(a, b, tech)] = np.nanmedian(np.array(ds), axis=0) if ds else np.full(N_BANDS, np.nan)
        return self.eq_cache[(a, b, tech)]

    def eq_init(self, path):
        """Static EQ for every pair. Bands that cannot be measured keep the nearest measured value (no cliffs)."""
        if path.exists():
            return np.load(path)
        n = len(self.d.cells)
        table = np.zeros((n, n, N_BANDS), dtype=np.float32)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            for a in range(n):
                for b in range(n):
                    if a != b:
                        table[a, b] = fill_nan(self.static_eq(a, b))
        table = np.clip(table, -EQ_LIMIT_DB, EQ_LIMIT_DB)
        np.save(path, table)
        return table

    @torch.no_grad()
    def run(self, model):
        from metrics import diff, rms_shape  # legacy metric, kept so numbers stay comparable with earlier runs
        model.eval()
        names = ("none", "static_eq", "model")
        wide = {k: [] for k in names}
        legacy = {k: [] for k in names}
        by_tech = {}
        eq_table = model.eq.detach().cpu().numpy()
        for key, rs, a, rt, b in self.pairs:
            nf = int(self.d.nf[rs])
            L = torch.from_numpy(self.d.frames[rs, :nf].astype(np.float32))[None].to(self.device)
            g, _ = model.gains(L, torch.tensor([a], device=self.device), torch.tensor([b], device=self.device))
            pred = clip_db((L + g)[0].cpu().numpy())
            src, tgt = self.d.clip[rs], self.d.clip[rt]
            fs, ft = self.d.floor[a], self.d.floor[b]
            tech = key.split("/")[0]
            static = fill_nan(self.static_eq(a, b))
            w = (rms_wide(src, tgt, src, fs, ft), rms_wide(src, tgt, clip_db(L[0].cpu().numpy()) + static, fs, ft),
                 rms_wide(src, tgt, pred, fs, ft))
            true_d = diff(src, tgt)
            lg = (rms_shape(true_d, np.zeros(N_BANDS)), rms_shape(true_d, static),
                  rms_shape(true_d, diff(src, pred)))
            for name, v1, v2 in zip(names, w, lg):
                wide[name].append(v1)
                legacy[name].append(v2)
            by_tech.setdefault(tech, []).append(w)
        model.train()
        out = {"n_pairs": len(self.pairs)}
        for k in names:
            out[k] = float(np.nanmean(wide[k]))              # main number: the wide metric
            out["legacy_" + k] = float(np.nanmean(legacy[k]))  # the old metric (45 dB below peak, 150-8000 Hz)
        out["by_technique"] = {t: {"none": float(np.nanmean([s[0] for s in v])), "static_eq": float(np.nanmean([s[1] for s in v])),
                                   "model": float(np.nanmean([s[2] for s in v])), "n": len(v)} for t, v in by_tech.items()}
        return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data_dir", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--steps", type=int, default=6000)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--ch", type=int, default=32)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--eval-every", type=int, default=1000)
    ap.add_argument("--ckpt-every", type=int, default=500)
    ap.add_argument("--hold-out", nargs="*", default=[])
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    warnings.filterwarnings("ignore", category=RuntimeWarning)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    data = FrameData(args.data_dir, hold_out_cells=args.hold_out)
    print(f"device {device} | cells {len(data.cells)} | train keys {len(data.train_keys)} | test keys {len(data.test_keys)}", flush=True)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    evaluator = Evaluator(data, device, np.random.default_rng(123))
    eq_init = evaluator.eq_init(args.out_dir / "eq_init.npy")
    model = SpecEQ(len(data.cells), ch=args.ch, eq_init=eq_init).to(device)
    print("parameters:", sum(p.numel() for p in model.parameters()), flush=True)
    other = [p for n_, p in model.named_parameters() if n_ != "eq"]
    opt = torch.optim.AdamW([{"params": other, "lr": args.lr, "weight_decay": 1e-4},
                             {"params": [model.eq], "lr": args.lr * 0.25, "weight_decay": 0.0}])
    warmup = 200
    sched = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda s: min(1.0, (s + 1) / warmup) * 0.5 * (1 + math.cos(math.pi * min(1.0, s / args.steps))))

    history, start = [], 0
    ckpt_path = args.out_dir / "ckpt.pt"
    if ckpt_path.exists():
        ck = torch.load(ckpt_path, map_location=device)
        model.load_state_dict(ck["model"]); opt.load_state_dict(ck["opt"]); sched.load_state_dict(ck["sched"])
        start, history = ck["step"], ck.get("history", [])
        rng = np.random.default_rng(args.seed + start)
        print(f"RESUMING from step {start}", flush=True)

    def meta():
        return {"cells": data.cells, "ch": args.ch, "dilations": list(model.dilations), "n_ctrl": N_CTRL, "lag": LAG,
                "max_delta_db": MAX_DELTA_DB, "eq_limit_db": EQ_LIMIT_DB}

    def save_ckpt(step):
        tmp = args.out_dir / "ckpt.tmp"
        torch.save({"model": model.state_dict(), "opt": opt.state_dict(), "sched": sched.state_dict(), "step": step,
                    "history": history, **meta()}, tmp)
        os.replace(tmp, ckpt_path)

    def do_eval(step):
        t0 = time.time()
        r = evaluator.run(model)
        r["step"] = step
        history.append(r)
        print(f"[eval @ {step}] shape error dB (lower is better), 100 Hz-16 kHz: no change {r['none']:.2f} | static EQ {r['static_eq']:.2f}"
              f" | model {r['model']:.2f}  ({r['n_pairs']} held-out pairs, {time.time() - t0:.0f}s)", flush=True)
        print(f"    old metric (150 Hz-8 kHz, for comparison): no change {r['legacy_none']:.2f} | static EQ {r['legacy_static_eq']:.2f}"
              f" | model {r['legacy_model']:.2f}", flush=True)
        for t, v in r["by_technique"].items():
            print(f"    {t:10s} n={v['n']:4d}  none {v['none']:.2f}  static {v['static_eq']:.2f}  model {v['model']:.2f}", flush=True)
        (args.out_dir / "results.json").write_text(json.dumps(history, indent=1), encoding="utf-8")

    do_eval(start)
    t_start = time.time()
    for step in range(start + 1, args.steps + 1):
        x, y, valid, a, b, fs, ft = data.sample(args.batch, rng)
        x, y, valid, fs, ft = (torch.from_numpy(v).to(device) for v in (x, y, valid, fs, ft))
        a, b = torch.from_numpy(a).to(device), torch.from_numpy(b).to(device)
        g, d = model.gains(x, a, b)
        loss, clip, seg = spec_loss(x, y, valid, g, d, fs, ft, model.freq_mask)
        loss = loss + 0.05 * ((model.eq[..., 1:] - model.eq[..., :-1]) ** 2).mean()  # no cliffs in the static EQ
        opt.zero_grad(set_to_none=True)
        loss.backward()
        nn.utils.clip_grad_norm_(other, 1.0)
        opt.step()
        sched.step()
        if step % 100 == 0:
            print(f"step {step}/{args.steps} loss {loss.item():.3f} (clip {clip:.3f} dB, segments {seg:.3f} dB) "
                  f"lr {sched.get_last_lr()[0]:.2e} {time.time() - t_start:.0f}s", flush=True)
        if step % args.ckpt_every == 0:
            save_ckpt(step)
        if step % args.eval_every == 0 and step != args.steps:
            do_eval(step)
    do_eval(args.steps)
    save_ckpt(args.steps)
    torch.save({"model": model.state_dict(), "step": args.steps, **meta()}, args.out_dir / "model.pt")
    print("done:", args.out_dir / "model.pt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
