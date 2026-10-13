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
import sys
import time
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
from metrics import diff, rms_shape  # noqa: E402

SCALE = 0.03  # training audio is level-normalised to about -30 dB RMS


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


class GuitarNet(nn.Module):
    def __init__(self, n_cells, ch=16, dilations=(1, 2, 4, 8, 16, 32, 64, 128, 256, 512), emb_dim=16, cond_dim=32):
        super().__init__()
        self.emb = nn.Embedding(n_cells, emb_dim)
        self.cond = nn.Sequential(nn.Linear(2 * emb_dim, cond_dim), nn.SiLU(),
                                  nn.Linear(cond_dim, cond_dim), nn.SiLU())
        self.inp = nn.Conv1d(1, ch, 1)
        self.blocks = nn.ModuleList([Block(ch, d, cond_dim) for d in dilations])
        self.out = nn.Sequential(nn.SiLU(), nn.Conv1d(ch, ch, 1), nn.SiLU(), nn.Conv1d(ch, 1, 1))
        nn.init.zeros_(self.out[-1].weight)  # starts as an exact "no change" filter
        nn.init.zeros_(self.out[-1].bias)

    def forward(self, x, src, tgt):
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


def spectral_loss(y, t):
    total = 0.0
    for n in (512, 1024, 2048, 4096):
        Y, T = stft_mag(y, n), stft_mag(t, n)
        total = total + F.l1_loss(torch.log(Y), torch.log(T)) + torch.norm(T - Y) / torch.norm(T)
    mr = total / 4.0
    # time-averaged spectrum: insensitive to timing differences between two performances
    Yp = stft_mag(y, 8192).pow(2).mean(-1)
    Tp = stft_mag(t, 8192).pow(2).mean(-1)
    Yp = F.avg_pool1d(Yp.unsqueeze(1), 9, stride=1, padding=4).squeeze(1)
    Tp = F.avg_pool1d(Tp.unsqueeze(1), 9, stride=1, padding=4).squeeze(1)
    ltas = F.l1_loss(torch.log(Yp + 1e-12), torch.log(Tp + 1e-12))
    return mr + ltas, mr.item(), ltas.item()


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

    def static_eq(self, a, b):
        """Median band difference b - a over the TRAINING notes (the best simple EQ we can measure)."""
        if (a, b) not in self.eq_cache:
            ds = []
            for key in self.d.train_keys:
                by_cell = self.d._usable(self.d.rows[key])
                if a in by_cell and b in by_cell:
                    ds.append(diff(self.bands(by_cell[a]), self.bands(by_cell[b])))
            self.eq_cache[(a, b)] = np.nanmedian(np.array(ds), axis=0) if ds else np.full(64, np.nan)
        return self.eq_cache[(a, b)]

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
            s = (rms_shape(true_d, np.zeros(64)), rms_shape(true_d, self.static_eq(a, b)),
                 rms_shape(true_d, diff(src_b, pred_b)))
            for name, v in zip(("none", "static_eq", "model"), s):
                scores[name].append(v)
            by_tech.setdefault(key.split("/")[0], []).append(s)
        model.train()
        out = {k: float(np.nanmean(v)) for k, v in scores.items()}
        out["n_pairs"] = len(self.pairs)
        out["by_technique"] = {t: {"none": float(np.nanmean([s[0] for s in v])),
                                   "static_eq": float(np.nanmean([s[1] for s in v])),
                                   "model": float(np.nanmean([s[2] for s in v])), "n": len(v)}
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
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    data = PairData(args.data_dir, hold_out_cells=args.hold_out)
    print(f"device {device} | cells {len(data.cells)} | train keys {len(data.train_keys)} | test keys {len(data.test_keys)}"
          f" | held-out cells {data.held_out_cells}")

    model = GuitarNet(len(data.cells), ch=args.ch).to(device)
    print("parameters:", sum(p.numel() for p in model.parameters()))
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    warmup = 300
    sched = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda s: min(1.0, (s + 1) / warmup) * 0.5 * (1 + math.cos(math.pi * min(1.0, s / args.steps))))

    args.out_dir.mkdir(parents=True, exist_ok=True)
    evaluator = Evaluator(data, device, np.random.default_rng(123))
    history = []

    def do_eval(step):
        t0 = time.time()
        r = evaluator.run(model)
        r["step"] = step
        history.append(r)
        print(f"[eval @ {step}] shape error dB (lower is better): no change {r['none']:.2f} | static EQ {r['static_eq']:.2f}"
              f" | model {r['model']:.2f}  ({r['n_pairs']} held-out pairs, {time.time() - t0:.0f}s)")
        for t, v in r["by_technique"].items():
            print(f"    {t:10s} n={v['n']:4d}  none {v['none']:.2f}  static {v['static_eq']:.2f}  model {v['model']:.2f}")
        (args.out_dir / "results.json").write_text(json.dumps(history, indent=1), encoding="utf-8")

    do_eval(0)  # the untrained model is an exact "no change", so this also checks the baselines
    t_start = time.time()
    for step in range(1, args.steps + 1):
        x, y, a, b = data.sample(args.batch, args.clip, rng)
        x, y = torch.from_numpy(x)[:, None].to(device), torch.from_numpy(y)[:, None].to(device)
        a, b = torch.from_numpy(a).to(device), torch.from_numpy(b).to(device)
        pred = model(x, a, b)
        loss, mr, ltas = spectral_loss(pred, y)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
        sched.step()
        if step % 200 == 0:
            print(f"step {step}/{args.steps} loss {loss.item():.3f} (stft {mr:.3f}, avg-spectrum {ltas:.3f}) "
                  f"lr {sched.get_last_lr()[0]:.2e} {time.time() - t_start:.0f}s")
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
