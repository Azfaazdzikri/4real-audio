"""Can the plugin find the SOURCE by itself from the spectrum of what it hears? (experiment for an "Auto" source)

    python training/nn/auto_source.py /content/spec_run2/model.pt /content/nn_frames /content/auto_out [--wav my_hss.wav ...]

Idea: every cell (guitar + pickup position) has a typical spectrum shape. The input's spectrum shape is compared with all of
them, the closest cells get a soft weight (softmax of -distance/tau) and the model is run with the WEIGHTED MIX of the source
embeddings and static EQs. Nothing needs to know which guitar it is: only whether the spectrum looks like a cell in the data.

Test: the held-out notes of the training evaluation (same pairs and same metric as train_spec.py). Prototypes are made from the
TRAINING notes only. Compared, per pair (shape error dB, lower is better):
    none      no change
    true      the model with the real source cell (what you get when the user picks the right one)
    auto      the model with the weights found from the input spectrum (soft and hard), for several tau
    wrong     the model with a random wrong source (what a bad pick costs)
The analysed part of the input is the first N seconds of the clip (the plugin only knows what it has heard so far).
--wav files (your own recordings, e.g. the HSS) are only analysed: the nearest cells and weights are printed.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))


def feature(clip_db_vec, mask, smooth):
    s = smooth(clip_db_vec)
    f = s[mask]
    return f - f.mean()


def main() -> int:
    import torch
    from train_spec import FrameData, SpecEQ, Evaluator, LAG
    from metrics import smooth_bands
    from spec_common import CENTERS, RATE, clip_db, frame_bands_db
    from spec_metrics import rms_wide, rms_wide64
    from spec_numpy import AutoSource
    from guitar_map import guitar_of

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("model", type=Path)
    ap.add_argument("data_dir", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--wav", nargs="*", default=[], help="your own recordings to analyse (mono or stereo wav)")
    ap.add_argument("--seconds", default="1,100", help="how much of the clip the detector may hear")
    ap.add_argument("--taus", default="0.5,1,2,4")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    ck = torch.load(args.model, map_location="cpu")
    net = SpecEQ(len(ck["cells"]), ch=ck["ch"], dilations=ck["dilations"], band_ch=ck.get("band_ch", 0))
    net.load_state_dict(ck["model"])
    net.eval()
    data = FrameData(args.data_dir)
    assert list(ck["cells"]) == list(data.cells), "the model and the frame data have different cells"
    n_cells = len(data.cells)
    rng = np.random.default_rng(args.seed)
    ev = Evaluator(data, torch.device("cpu"), np.random.default_rng(0))
    secs = [float(s) for s in args.seconds.split(",")]
    taus = [float(t) for t in args.taus.split(",")]
    hop_s = 512.0 / RATE
    masks = {"mid 150-8k": (CENTERS >= 150) & (CENTERS <= 8000), "wide 150-12k": (CENTERS >= 150) & (CENTERS <= 12000)}

    # ---- prototypes: mean feature of every cell over the TRAINING notes
    train_rows = {c: [] for c in range(n_cells)}
    for key in data.train_keys:
        for c, row in data._usable(data.rows[key]).items():
            train_rows[c].append(row)
    protos = {}
    for name, mask in masks.items():
        P = np.zeros((n_cells, int(mask.sum())))
        for c in range(n_cells):
            fs = [feature(data.clip[r], mask, smooth_bands) for r in train_rows[c]]
            P[c] = np.mean(fs, axis=0)
        protos[name] = P

    online = AutoSource(protos["wide 150-12k"], masks["wide 150-12k"], tau_db=0.5)   # what the plugin runs (gated running mean)

    def weights(L_frames_db, name, tau):
        """Soft weights over cells from frames (n, 64) dB."""
        f = feature(clip_db(L_frames_db), masks[name], smooth_bands)
        d = np.sqrt(np.mean((protos[name] - f) ** 2, axis=1))      # dB rms distance to every cell
        z = -(d - d.min()) / tau
        w = np.exp(z)
        return w / w.sum(), d

    # ---- the model with mixed source (same maths as SpecEQ.gains)
    @torch.no_grad()
    def run_mix(L, w, b):
        wt = torch.from_numpy(np.asarray(w, dtype=np.float32))
        e_src = (wt[:, None] * net.emb.weight).sum(0, keepdim=True)
        e_tgt = net.emb(torch.tensor([b]))
        c = net.cond(torch.cat([e_src, e_tgt], dim=-1))
        h = net.embed(L)
        skip = 0
        for blk in net.blocks:
            h, s = blk(h, c)
            skip = skip + s
        from train_spec import MAX_DELTA_DB
        out = MAX_DELTA_DB * torch.tanh(net.head(skip) / MAX_DELTA_DB)
        d = net.zero_mean(out.transpose(1, 2) @ net.ctrl_w)
        d = torch.nn.functional.pad(d, (0, 0, LAG, 0))[:, :L.shape[1]]
        eq_all = net.eq_curve_db(torch.arange(n_cells), torch.full((n_cells,), b))          # (cells, 64)
        eq = (wt[:, None] * eq_all).sum(0)
        return eq + d

    def onehot(a):
        w = np.zeros(n_cells)
        w[a] = 1.0
        return w

    # ---- evaluation on the held-out pairs
    results = {}
    pair_count = 0
    for T in secs:
        nfr = max(8, int(round(T / hop_s)))
        acc = {"top1_cell": [], "top1_guitar": [], "w_true": [], "entropy": [], "dist_min": []}
        err = {"none": [], "true": [], "wrong": []}
        err["online (plugin: loud frames, running mean)"] = []
        for name in masks:
            for tau in taus:
                err[f"auto {name} tau={tau:g}"] = []
                err[f"hard {name} tau={tau:g}"] = []
        for key, rs, a, rt, b in ev.pairs:
            nf = int(data.nf[rs])
            Lf = data.frames[rs, :nf].astype(np.float32)
            heard = Lf[:min(nf, nfr)]
            L = torch.from_numpy(Lf)[None]
            src, tgt = data.clip[rs], data.clip[rt]
            fs, ft = data.floor[a], data.floor[b]

            def score(w):
                g = run_mix(L, w, b)
                return rms_wide(src, tgt, clip_db((L + g)[0].numpy()), fs, ft)
            err["none"].append(rms_wide(src, tgt, src, fs, ft))
            err["true"].append(score(onehot(a)))
            wrong = [c for c in range(n_cells) if c not in (a, b)]
            err["wrong"].append(score(onehot(int(rng.choice(wrong)))))
            err["online (plugin: loud frames, running mean)"].append(score(online.run(heard)[-1]))
            for name in masks:
                for tau in taus:
                    w, d = weights(heard, name, tau)
                    err[f"auto {name} tau={tau:g}"].append(score(w))
                    err[f"hard {name} tau={tau:g}"].append(score(onehot(int(np.argmax(w)))))
                    if name == "mid 150-8k" and tau == taus[1]:
                        top = int(np.argmax(w))
                        acc["top1_cell"].append(float(top == a))
                        acc["top1_guitar"].append(float(guitar_of(data.cells[top]) == guitar_of(data.cells[a])))
                        acc["w_true"].append(float(w[a]))
                        acc["entropy"].append(float(-(w * np.log(w + 1e-12)).sum()))
                        acc["dist_min"].append(float(d.min()))
            pair_count += 1
        res = {k: float(np.nanmean(v)) for k, v in err.items()}
        res.update({k: float(np.mean(v)) for k, v in acc.items()})
        results[str(T)] = res
        print(f"\n=== detector hears the first {T:g} s ({len(ev.pairs)} held-out pairs; shape error dB, lower is better) ===")
        print(f"  none {res['none']:.2f} | true source {res['true']:.2f} | wrong source {res['wrong']:.2f}")
        print(f"  ONLINE estimator used by the plugin (gate, running mean, tau 0.5): {res['online (plugin: loud frames, running mean)']:.2f}")
        for name in masks:
            for tau in taus:
                print(f"  features {name:13s} tau {tau:3g}: auto soft {res[f'auto {name} tau={tau:g}']:.2f} | hard {res[f'hard {name} tau={tau:g}']:.2f}")
        print(f"  (mid, tau {taus[1]:g}) top-1 cell right {100 * res['top1_cell']:.0f} % | top-1 guitar right {100 * res['top1_guitar']:.0f} %"
              f" | weight on the true cell {res['w_true']:.2f} | entropy {res['entropy']:.2f} | nearest distance {res['dist_min']:.2f} dB")

    # ---- your own recordings
    if args.wav:
        from preprocess_wav import read_wav, to_mono
        print("\n=== your recordings: nearest cells (features mid 150-8k) ===")
        for p in args.wav:
            x, rate = read_wav(Path(p))
            mono, _ = to_mono(x)
            if rate != RATE:
                print(f"  {p}: {rate} Hz, not {RATE}: skipped (export it at 48 kHz)")
                continue
            fr = frame_bands_db(mono.astype(np.float64))
            for T in secs:
                heard = fr[:max(8, int(round(T / hop_s)))]
                w, d = weights(heard, "mid 150-8k", taus[1])
                order = np.argsort(-w)[:4]
                txt = " | ".join(f"{data.cells[i].replace('|', ' ')} {w[i]:.2f} (d={d[i]:.1f} dB)" for i in order)
                print(f"  {Path(p).name} (first {T:g} s): {txt}")

    (args.out_dir / "auto_source_results.json").write_text(json.dumps(results, indent=1))
    np.savez(args.out_dir / "prototypes.npz", cells=np.array(data.cells), **{k.split()[0]: v for k, v in protos.items()})
    print(f"\nsaved {args.out_dir}/auto_source_results.json and prototypes.npz")
    return 0


if __name__ == "__main__":
    sys.exit(main())
