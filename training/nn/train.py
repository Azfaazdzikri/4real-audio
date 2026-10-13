"""Train a conditional causal TCN (WaveNet style) that turns one guitar's sound into another's.

    python training/nn/train.py /content/nn_data /content/nn_run --steps 20000

Input : clean guitar audio recorded on SOURCE cell a (guitar + pickup position)
Output: the same note/chord as it sounds on TARGET cell b
Pairs : the same note or chord (string/fret, chord name) recorded on two different cells.
        Recordings are not sample-aligned (different performances), so the loss compares spectra
        (multi-resolution STFT + time-averaged spectrum), not waveforms.
Test  : notes/chords held out of training in every cell; reports the spectral-shape error against
        "no change" and against a static EQ measured on the training notes.
Needs torch (Colab has it) and numpy.
"""
import argparse
import json
import math
import os
import sys
import time
import warnings
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
from data import PairData  # noqa: E402
from extract_features import describe  # noqa: E402
from metrics import BAND_CENTERS, RANGE, diff, rms_shape  # noqa: E402

SCALE = 0.03  # training audio is level-normalised to about -30 dB RMS
RATE = 48000
N_BANDS = 64
EQ_LIMIT_DB = 15.0


class Block(nn.Module):
    def __init__(self, ch, dilation, cond_dim, kernel=3):
        super().__init__()
        self.pad = (kernel - 1) * dilation
        self.conv = nn.Conv1d(ch, 2 * ch, kernel, dilation=dilation)
        self.film = nn.Linear(cond_dim, 4 * ch)  # scale and shift for both halves of the gated conv
        self.res = nn.Conv1d(ch, ch, 1)
        self.skip = nn.Conv1d(ch, ch, 1)

    def forward(self, x, cond):
        h = self.conv(F.pad(x, (self.pad, 0)))
        gamma, beta = self.film(cond).unsqueeze(-1).chunk(2, dim=1)
        h = h * (1.0 + gamma) + beta
        a, b = h.chunk(2, dim=1)
        z = torch.tanh(a) * torch.sigmoid(b)
        return x + self.res(z), self.skip(z)


_INTERP = {}


def interp_weights(T, device):
    """Linear interpolation (on a log-frequency axis) from the 64 band centres to the rfft bins of length T."""
    key = (T, str(device))
    if key not in _INTERP:
        f = torch.fft.rfftfreq(T, 1.0 / RATE).clamp(float(BAND_CENTERS[0]), float(BAND_CENTERS[-1]))
        step = math.log(BAND_CENTERS[1] / BAND_CENTERS[0])
        pos = ((torch.log(f) - math.log(BAND_CENTERS[0])) / step).clamp(0, N_BANDS - 1)
        i0 = pos.floor().long()
        i1 = (i0 + 1).clamp(max=N_BANDS - 1)
        _INTERP[key] = (i0.to(device), i1.to(device), (pos - i0.float()).to(device))
    return _INTERP[key]


class GuitarNet(nn.Module):
    """Learned per-pair EQ (starts at the measured static EQ) + conditional causal TCN residual."""

    def __init__(self, n_cells, ch=16, dilations=(1, 2, 4, 8, 16, 32, 64, 128, 256, 512), emb_dim=16, cond_dim=32,
                 eq_init=None):
        super().__init__()
        init = torch.zeros(n_cells, n_cells, N_BANDS) if eq_init is None else torch.as_tensor(eq_init, dtype=torch.float32)
        self.eq = nn.Parameter(init.clone())  # dB per band for (source cell, target cell)
        self.use_net = True
        self.register_buffer("range_mask", torch.as_tensor(RANGE, dtype=torch.bool))
        self.emb = nn.Embedding(n_cells, emb_dim)
        self.cond = nn.Sequential(nn.Linear(2 * emb_dim, cond_dim), nn.SiLU(),
                                  nn.Linear(cond_dim, cond_dim), nn.SiLU())
        self.inp = nn.Conv1d(1, ch, 1)
        self.blocks = nn.ModuleList([Block(ch, d, cond_dim) for d in dilations])
        self.out = nn.Sequential(nn.SiLU(), nn.Conv1d(ch, ch, 1), nn.SiLU(), nn.Conv1d(ch, 1, 1))
        nn.init.zeros_(self.out[-1].weight)  # starts as an exact "no change" filter
        nn.init.zeros_(self.out[-1].bias)

    def eq_curve_db(self, src, tgt):
        g = self.eq[src, tgt].clamp(-EQ_LIMIT_DB, EQ_LIMIT_DB)
        return g - g[:, self.range_mask].mean(dim=1, keepdim=True)  # shape only: no level change

    def apply_eq(self, x, src, tgt):
        T = x.shape[-1]
        i0, i1, w = interp_weights(T, x.device)
        g = self.eq_curve_db(src, tgt)
        gain = 10.0 ** ((g[:, i0] * (1 - w) + g[:, i1] * w) / 20.0)
        return torch.fft.irfft(torch.fft.rfft(x.squeeze(1)) * gain, n=T).unsqueeze(1)

    def forward(self, x, src, tgt):
        x = self.apply_eq(x, src, tgt)
        if not self.use_net:  # EQ-only experiment (cheap enough for a CPU)
            return x
        c = self.cond(torch.cat([self.emb(src), self.emb(tgt)], dim=-1))
        h = self.inp(x / SCALE)
        skip = 0
        for block in self.blocks:
            h, s = block(h, c)
            skip = skip + s
        return x + SCALE * self.out(skip)


def stft_mag(x, n):
    window = torch.hann_window(n, device=x.device)
    return torch.stft(x.squeeze(1), n, hop_length=n // 4, win_length=n, window=window,
                      return_complex=True).abs() + 1e-7


_BAND_W = {}


def band_matrix(device):
    """(4097, 64) averaging matrix: power spectrum bins of an 8192 FFT -> the 64 log bands used by the metric."""
    if str(device) not in _BAND_W:
        freqs = np.fft.rfftfreq(8192, 1.0 / RATE)
        edges = np.geomspace(70.0, 19000.0, N_BANDS + 1)
        W = np.zeros((len(freqs), N_BANDS), dtype=np.float32)
        for i in range(N_BANDS):
            sel = (freqs >= edges[i]) & (freqs < edges[i + 1])
            if not sel.any():
                sel = np.zeros(len(freqs), dtype=bool)
                sel[int(np.argmin(np.abs(freqs - BAND_CENTERS[i])))] = True
            W[sel, i] = 1.0 / sel.sum()
        _BAND_W[str(device)] = torch.from_numpy(W).to(device)
    return _BAND_W[str(device)]


def band_db(x):
    """Time-averaged spectrum in the 64 metric bands (dB), 3-band smoothed in the power domain."""
    p = stft_mag(x, 8192).pow(2).mean(-1) @ band_matrix(x.device)
    p = F.avg_pool1d(F.pad(p.unsqueeze(1), (1, 1), mode="replicate"), 3, stride=1).squeeze(1)
    return 10.0 * torch.log10(p + 1e-14)


def spectral_loss(y, t, range_mask):
    """Main term = the evaluation metric itself (shape-only RMS over the 64 bands); STFT terms regularise."""
    total = 0.0
    for n in (512, 1024, 2048, 4096):
        Y, T = stft_mag(y, n), stft_mag(t, n)
        total = total + F.l1_loss(torch.log(Y), torch.log(T)) + torch.norm(T - Y) / torch.norm(T)
    mr = total / 4.0
    yb, tb = band_db(y), band_db(t)
    mask = (tb > tb.max(dim=1, keepdim=True).values - 45.0) & range_mask[None, :]
    d = (yb - tb) * mask
    d = (d - (d.sum(1, keepdim=True) / mask.sum(1, keepdim=True).clamp(min=1))) * mask
    band = torch.sqrt((d ** 2).sum(1) / mask.sum(1).clamp(min=1) + 1e-6).mean()
    return band + 0.3 * mr, mr.item(), band.item()


class Evaluator:
    def __init__(self, data: PairData, device, rng):
        self.d = data
        self.device = device
        self.pairs = data.test_pairs(rng, per_key=6)
        self.cache = {}
        self.eq_cache = {}

    def bands(self, row, signal=None):
        if signal is not None:
            return describe(signal.astype(np.float64))["bands_body_db"]
        if row not in self.cache:
            n = self.d.items[row]["n"]
            self.cache[row] = describe(self.d.audio[row, :n].astype(np.float64))["bands_body_db"]
        return self.cache[row]

    def static_eq(self, a, b, tech=None):
        """Median band difference b - a over the TRAINING notes (the best simple EQ we can measure).
        tech: only use notes of that technique (an oracle: the plugin cannot know the technique)."""
        if (a, b, tech) not in self.eq_cache:
            ds = []
            for key in self.d.train_keys:
                if tech is not None and key.split("/")[0] != tech:
                    continue
                by_cell = self.d._usable(self.d.rows[key])
                if a in by_cell and b in by_cell:
                    ds.append(diff(self.bands(by_cell[a]), self.bands(by_cell[b])))
            self.eq_cache[(a, b, tech)] = np.nanmedian(np.array(ds), axis=0) if ds else np.full(64, np.nan)
        return self.eq_cache[(a, b, tech)]

    def eq_init(self, n_cells, path):
        """Static EQ (dB) for every cell pair, used as the starting point of the model's EQ branch."""
        if path.exists():
            return np.load(path)
        import warnings
        table = np.zeros((n_cells, n_cells, 64), dtype=np.float32)
        t0 = time.time()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            for a in range(n_cells):
                for b in range(n_cells):
                    if a != b:
                        table[a, b] = np.nan_to_num(self.static_eq(a, b), nan=0.0)
                print(f"  static EQ init: source cell {a + 1}/{n_cells} ({time.time() - t0:.0f}s)", flush=True)
        table = np.clip(table, -EQ_LIMIT_DB, EQ_LIMIT_DB)
        np.save(path, table)
        return table

    @torch.no_grad()
    def run(self, model):
        model.eval()
        scores = {"none": [], "static_eq": [], "model": []}
        by_tech = {}
        for key, src_row, a, tgt_row, b in self.pairs:
            n = self.d.items[src_row]["n"]
            x = torch.from_numpy(self.d.audio[src_row].astype(np.float32))[None, None].to(self.device)
            y = model(x, torch.tensor([a], device=self.device), torch.tensor([b], device=self.device))
            y = y[0, 0, :n].cpu().numpy()
            src_b, tgt_b = self.bands(src_row), self.bands(tgt_row)
            pred_b = self.bands(None, y)
            true_d = diff(src_b, tgt_b)
            tech = key.split("/")[0]
            s = (rms_shape(true_d, np.zeros(64)), rms_shape(true_d, self.static_eq(a, b)),
                 rms_shape(true_d, diff(src_b, pred_b)), rms_shape(true_d, self.static_eq(a, b, tech)))
            for name, v in zip(("none", "static_eq", "model"), s):
                scores[name].append(v)
            by_tech.setdefault(tech, []).append(s)
        model.train()
        out = {k: float(np.nanmean(v)) for k, v in scores.items()}
        out["n_pairs"] = len(self.pairs)
        out["by_technique"] = {t: {"none": float(np.nanmean([s[0] for s in v])),
                                   "static_eq": float(np.nanmean([s[1] for s in v])),
                                   "model": float(np.nanmean([s[2] for s in v])),
                                   "static_tech": float(np.nanmean([s[3] for s in v])), "n": len(v)}
                               for t, v in by_tech.items()}
        return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data_dir", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--steps", type=int, default=20000)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--clip", type=int, default=48000)
    ap.add_argument("--ch", type=int, default=16)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--eval-every", type=int, default=5000)
    ap.add_argument("--hold-out", nargs="*", default=[], help="cell name fragments never used in training")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--eq-only", action="store_true", help="train only the per-pair EQ (no TCN); runs on a CPU")
    ap.add_argument("--ckpt-every", type=int, default=1000, help="save a resumable checkpoint every N steps")
    args = ap.parse_args()
    warnings.filterwarnings("ignore", category=RuntimeWarning)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    data = PairData(args.data_dir, hold_out_cells=args.hold_out)
    print(f"device {device} | cells {len(data.cells)} | train keys {len(data.train_keys)} | test keys {len(data.test_keys)}"
          f" | held-out cells {data.held_out_cells}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    evaluator = Evaluator(data, device, np.random.default_rng(123))
    eq_init = evaluator.eq_init(len(data.cells), args.out_dir / "eq_init.npy")
    model = GuitarNet(len(data.cells), ch=args.ch, eq_init=eq_init).to(device)
    model.use_net = not args.eq_only
    print("parameters:", sum(p.numel() for p in model.parameters()), "| EQ only" if args.eq_only else "")
    eq_params = [model.eq]
    other = [p for n_, p in model.named_parameters() if n_ != "eq"]
    opt = torch.optim.AdamW([{"params": other, "lr": args.lr, "weight_decay": 1e-4},
                             {"params": eq_params, "lr": args.lr * 0.25, "weight_decay": 0.0}])
    warmup = 300
    sched = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda s: min(1.0, (s + 1) / warmup) * 0.5 * (1 + math.cos(math.pi * min(1.0, s / args.steps))))

    history = []
    start_step = 0
    ckpt_path = args.out_dir / "ckpt.pt"
    if ckpt_path.exists():  # resume after a disconnect
        ck = torch.load(ckpt_path, map_location=device)
        model.load_state_dict(ck["model"])
        opt.load_state_dict(ck["opt"])
        sched.load_state_dict(ck["sched"])
        start_step = ck["step"]
        history = ck.get("history", [])
        rng = np.random.default_rng(args.seed + start_step)
        print(f"RESUMING from step {start_step}", flush=True)

    def save_ckpt(step):
        tmp = args.out_dir / "ckpt.tmp"
        torch.save({"model": model.state_dict(), "opt": opt.state_dict(), "sched": sched.state_dict(),
                    "step": step, "history": history, "cells": data.cells, "ch": args.ch}, tmp)
        os.replace(tmp, ckpt_path)

    def do_eval(step):
        t0 = time.time()
        r = evaluator.run(model)
        r["step"] = step
        history.append(r)
        print(f"[eval @ {step}] shape error dB (lower is better): no change {r['none']:.2f} | static EQ {r['static_eq']:.2f}"
              f" | model {r['model']:.2f}  ({r['n_pairs']} held-out pairs, {time.time() - t0:.0f}s)")
        for t, v in r["by_technique"].items():
            print(f"    {t:10s} n={v['n']:4d}  none {v['none']:.2f}  static {v['static_eq']:.2f}"
                  f"  static-per-technique(oracle) {v['static_tech']:.2f}  model {v['model']:.2f}")
        (args.out_dir / "results.json").write_text(json.dumps(history, indent=1), encoding="utf-8")

    do_eval(start_step)  # fresh run: model starts at the measured static EQ, so it must score like "static EQ" here
    t_start = time.time()
    for step in range(start_step + 1, args.steps + 1):
        x, y, a, b = data.sample(args.batch, args.clip, rng)
        x, y = torch.from_numpy(x)[:, None].to(device), torch.from_numpy(y)[:, None].to(device)
        a, b = torch.from_numpy(a).to(device), torch.from_numpy(b).to(device)
        pred = model(x, a, b)
        loss, mr, band = spectral_loss(pred, y, model.range_mask)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        nn.utils.clip_grad_norm_(other, 1.0)  # the EQ table has its own small learning rate
        opt.step()
        sched.step()
        if step % 200 == 0:
            print(f"step {step}/{args.steps} loss {loss.item():.3f} (stft {mr:.3f}, band-shape dB {band:.3f}) "
                  f"lr {sched.get_last_lr()[0]:.2e} {time.time() - t_start:.0f}s")
        if step % args.ckpt_every == 0:
            save_ckpt(step)
        if step % args.eval_every == 0 and step != args.steps:
            do_eval(step)
            torch.save({"state": model.state_dict(), "cells": data.cells, "ch": args.ch, "step": step},
                       args.out_dir / "model.pt")
    do_eval(args.steps)
    torch.save({"state": model.state_dict(), "cells": data.cells, "ch": args.ch, "step": args.steps},
               args.out_dir / "model.pt")
    print("saved", args.out_dir / "model.pt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
