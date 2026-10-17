"""Listen to what the v3 model does, and check that the filtered AUDIO matches what the training measured.

    python training/nn/listen.py /content/spec_run/model.pt /content/nn_frames "/content/drive/MyDrive/Data Training" /content/listen --pairs 12

For held-out notes/chords it writes, per pair, in out_dir/NN_<technique>_<note>/ :
    1_source.wav   the recording on the source guitar (what you would play)
    2_static.wav   source + static EQ only
    3_model.wav    source + static EQ + dynamic correction (what the plugin does)
    4_target.wav   the real recording of the same note on the target guitar (the goal)
All four are level matched (-30 dB reference) so you can A/B them. Prints, per pair, the spectral-shape error (dB, lower is
better) of: no change, static EQ, model as predicted in training, and the model measured on the real filtered audio.
Also self-checks the numpy network against torch.
"""
import argparse
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
from metrics import diff  # noqa: E402
from spec_metrics import rms_wide  # noqa: E402
from preprocess_wav import read_wav, to_mono, trim, write_wav24  # noqa: E402
from spec_common import RATE, clip_db, frame_bands_db  # noqa: E402
from spec_numpy import SpecNumpy, process  # noqa: E402
from prepare_frames import limit_for  # noqa: E402


def load_clip(root: Path, cell: str, key: str):
    g, pos = cell.split("|")
    path = root / g / pos / key
    x, rate = read_wav(path)
    mono, _ = to_mono(x)
    return trim(mono, rate, limit_for(path.relative_to(root).parts)).astype(np.float64)


def main() -> int:
    import torch
    from train_spec import FrameData, SpecEQ

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("model", type=Path)
    ap.add_argument("data_dir", type=Path)
    ap.add_argument("wav_root", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--pairs", type=int, default=12)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--scales", default="0.7,1.3", help="extra files 5_x<scale>.wav with the EQ intensity multiplied (1.0 = as trained)")
    args = ap.parse_args()

    ck = torch.load(args.model, map_location="cpu")
    net = SpecEQ(len(ck["cells"]), ch=ck["ch"], dilations=ck["dilations"])
    net.load_state_dict(ck["model"])
    net.eval()
    state = {k: v.numpy() for k, v in ck["model"].items()}
    ref = SpecNumpy(state, ck)
    data = FrameData(args.data_dir)

    # 1) the numpy network (= the plugin's algorithm) must agree with torch
    rs = data.rows[data.test_keys[0]]
    a, b = list(rs)[:2]
    nf = int(data.nf[rs[a]])
    L = data.frames[rs[a], :nf].astype(np.float32)
    with torch.no_grad():
        g_t, d_t = net.gains(torch.from_numpy(L)[None], torch.tensor([a]), torch.tensor([b]), lag=0)
    d_n = ref.delta(L.astype(np.float64), a, b)
    err = float(np.max(np.abs(d_t[0].numpy() - d_n)))
    print(f"numpy vs torch network: max difference {err:.2e} dB", "(OK)" if err < 1e-3 else "(MISMATCH - tell Claude)")

    # 2) listening pairs: spread over techniques, always a different guitar (not just another pickup of the same guitar)
    rng = np.random.default_rng(args.seed)
    pairs = [p for p in data.test_pairs(np.random.default_rng(5), per_key=3)
             if data.items[p[1]]["cell"].split("|")[0] != data.items[p[3]]["cell"].split("|")[0]]
    by_tech = {}
    for p in pairs:
        by_tech.setdefault(p[0].split("/")[0], []).append(p)
    chosen = []
    while len(chosen) < args.pairs and any(by_tech.values()):
        for t in sorted(by_tech):
            if by_tech[t] and len(chosen) < args.pairs:
                chosen.append(by_tech[t].pop(int(rng.integers(len(by_tech[t])))))

    print(f"\n{'pair':58s} {'none':>6s} {'static':>7s} {'model(train)':>13s} {'model(audio)':>13s}")
    rows = []
    for n, (key, rs_, a, rt, b) in enumerate(chosen, 1):
        src_item, tgt_item = data.items[rs_], data.items[rt]
        x = load_clip(args.wav_root, src_item["cell"], key)
        t = load_clip(args.wav_root, tgt_item["cell"], key)
        gs, gt = src_item["gain_db"], tgt_item["gain_db"]
        y_model, _ = process(ref, x, a, b, gs, dynamic=True)
        y_static, _ = process(ref, x, a, b, gs, dynamic=False)
        k = 10.0 ** (gs / 20.0)

        def bands(sig, gain=0.0):
            return clip_db(frame_bands_db(sig * 10.0 ** (gain / 20.0)))
        src_b, tgt_b = bands(x, gs), bands(t, gt)
        true_d = diff(src_b, tgt_b)
        fs_, ft_ = data.floor[a], data.floor[b]
        s_none = rms_wide(src_b, tgt_b, src_b, fs_, ft_)
        s_static = rms_wide(src_b, tgt_b, bands(y_static, gs), fs_, ft_)
        s_audio = rms_wide(src_b, tgt_b, bands(y_model, gs), fs_, ft_)
        # what the training evaluation predicts for the same pair
        with torch.no_grad():
            Lf = torch.from_numpy(data.frames[rs_, :int(data.nf[rs_])].astype(np.float32))[None]
            g, _ = net.gains(Lf, torch.tensor([a]), torch.tensor([b]))
        s_train = rms_wide(src_b, tgt_b, clip_db((Lf + g)[0].numpy()), fs_, ft_)
        name = f"{n:02d}_{key.replace('/', '_').replace('.wav', '')}"
        label = f"{src_item['cell'].split('|')[0][:14]} -> {tgt_item['cell'].split('|')[0][:14]} {key[:24]}"
        print(f"{label:58s} {s_none:6.2f} {s_static:7.2f} {s_train:13.2f} {s_audio:13.2f}")
        rows.append((s_none, s_static, s_train, s_audio))
        d = args.out_dir / name
        write_wav24(d / "1_source.wav", np.clip(x * k, -1, 1), RATE)
        write_wav24(d / "2_static.wav", np.clip(y_static * k, -1, 1), RATE)
        write_wav24(d / "3_model.wav", np.clip(y_model * k, -1, 1), RATE)
        for sc in [float(v) for v in args.scales.split(",") if v.strip()]:
            ref.intensity = sc
            y_sc, _ = process(ref, x, a, b, gs, dynamic=True)
            s_sc = rms_wide(src_b, tgt_b, bands(y_sc, gs), fs_, ft_)
            print(f"{'':58s} intensity x{sc:g}: {s_sc:.2f}")
            write_wav24(d / f"5_x{sc:g}.wav", np.clip(y_sc * k, -1, 1), RATE)
        ref.intensity = 1.0
        write_wav24(d / "4_target.wav", np.clip(t * 10.0 ** (gt / 20.0), -1, 1), RATE)
    m = np.nanmean(np.array(rows), axis=0)
    print(f"\nmean: none {m[0]:.2f} | static {m[1]:.2f} | model (training eval) {m[2]:.2f} | model (real filtered audio) {m[3]:.2f}")
    print("If 'model(audio)' is close to 'model(train)', what training measured is what the plugin will produce.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
