"""Export a trained model.pt to .ngm files (Neural Guitar Modeler): one file per TARGET guitar.

    python training/nn/export_ngm.py model.pt out_dir

A .ngm file is JSON (like Neural Amp Modeler's .nam): architecture config, the shared network weights, and the
conditioning of every (source cell -> target cell) pair already worked out, so the plugin needs no embedding
tables or conditioning MLP, only the causal convolution stack:

    per pair: film[block] (4*ch numbers: gamma, beta of the gated conv) and eq_db (64 dB values, shape only)

Also writes golden test vectors (golden_<CODE>.json) made with torch, so the C++ engine can be checked sample by sample.
Needs torch and numpy (run it on Colab, CPU is fine). The signal flow it describes:

    z   = EQ(x)                                  # per-pair 64-band EQ, applied in the plugin as a FIR
    h   = inp(z / input_scale)
    per block: g = conv_dilated(h); g = g * (1 + gamma) + beta; z = tanh(g[:ch]) * sigmoid(g[ch:])
               h = h + res(z); skip += skip_conv(z)
    out = z0 + input_scale * out_net(skip),  out_net = SiLU -> conv1x1 -> SiLU -> conv1x1
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from train import GuitarNet, N_BANDS, SCALE  # noqa: E402
from metrics import BAND_CENTERS  # noqa: E402

FORMAT_VERSION = "0.1.0"

# Plugin guitar codes in the order the plugin lists them, with the selector positions that exist in the data.
GUITARS = [
    ("LP", ["bridge", "middle", "neck"]),
    ("335", ["neck"]),
    ("SG", ["bridge", "neck"]),
    ("PRS", ["bridge", "middle", "neck"]),
    ("RIC", ["bridge"]),
    ("STR", ["bridge", "bridge-middle", "middle-neck", "neck"]),
    ("TCR", ["bridge", "neck"]),
    ("FLC", ["middle"]),
]


def guitar_of(cell: str):
    s = cell.lower()
    if "g6136" in s or "falcon" in s:
        return "FLC"
    if "335" in s:
        return "335"
    if "gibson sg" in s or " sg " in s:
        return "SG"
    if "prs" in s:
        return "PRS"
    if "rickenbacker" in s:
        return "RIC"
    if "strato" in s:
        return "STR"
    if "telecaster" in s:
        return "TCR"
    if "lespaul" in s or "les paul" in s:
        return "LP"
    raise ValueError(f"unknown guitar in cell name: {cell}")


def position_of(cell: str):
    p = cell.split("|", 1)[1].lower()
    if "bridge-middle" in p:
        return "bridge-middle"
    if "middle-neck" in p:
        return "middle-neck"
    if "bridge" in p:
        return "bridge"
    if "neck" in p:
        return "neck"
    if "middle" in p:
        return "middle"
    raise ValueError(f"unknown position in cell name: {cell}")


def flat(t, digits=7):
    return [float(f"{v:.{digits}g}") for v in t.detach().cpu().numpy().astype(np.float32).ravel()]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("model", type=Path)
    ap.add_argument("out_dir", type=Path)
    args = ap.parse_args()

    ck = torch.load(args.model, map_location="cpu")
    cells = ck["cells"]
    ch = ck["ch"]
    model = GuitarNet(len(cells), ch=ch)
    model.load_state_dict(ck["state"])
    model.eval()

    # cell index -> (guitar code, position index within that guitar)
    where = {}
    for i, cell in enumerate(cells):
        code, pos = guitar_of(cell), position_of(cell)
        order = dict(GUITARS)[code]
        where[i] = (code, order.index(pos))
    assert len(set(where.values())) == len(cells) == 17, "every cell must map to a distinct plugin position"

    # sources in the plugin's own order (guitar list order, then position order)
    sources = sorted(range(len(cells)), key=lambda i: ([g for g, _ in GUITARS].index(where[i][0]), where[i][1]))

    n_blocks = len(model.blocks)
    dilations = [int(b.conv.dilation[0]) for b in model.blocks]
    kernel = int(model.blocks[0].conv.kernel_size[0])
    weights = {"inp.weight": model.inp.weight, "inp.bias": model.inp.bias}
    for i, b in enumerate(model.blocks):
        for nm, mod in (("conv", b.conv), ("res", b.res), ("skip", b.skip)):
            weights[f"blocks.{i}.{nm}.weight"] = mod.weight
            weights[f"blocks.{i}.{nm}.bias"] = mod.bias
    weights["out.0.weight"] = model.out[1].weight  # after the first SiLU
    weights["out.0.bias"] = model.out[1].bias
    weights["out.1.weight"] = model.out[3].weight
    weights["out.1.bias"] = model.out[3].bias
    net = {k: {"shape": list(v.shape), "data": flat(v)} for k, v in weights.items()}

    args.out_dir.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(1234)
    t = np.arange(4800) / 48000.0
    probe = (0.03 * (np.sin(2 * np.pi * 110 * t) + 0.5 * np.sin(2 * np.pi * 330 * t) + 0.3 * np.sin(2 * np.pi * 1210 * t))
             * np.exp(-3 * t) + 0.002 * rng.standard_normal(len(t))).astype(np.float32)

    for code, order in GUITARS:
        targets = [i for i in range(len(cells)) if where[i][0] == code]
        targets.sort(key=lambda i: where[i][1])
        cond = []
        with torch.no_grad():
            for ti in targets:
                row = []
                for si in sources:
                    s, tg = torch.tensor([si]), torch.tensor([ti])
                    c = model.cond(torch.cat([model.emb(s), model.emb(tg)], dim=-1))
                    row.append({"film": [flat(b.film(c)[0]) for b in model.blocks],
                                "eq_db": flat(model.eq_curve_db(s, tg)[0])})
                cond.append(row)
        doc = {
            "format": "ngm",
            "version": FORMAT_VERSION,
            "metadata": {
                "name": f"4real-audio {code}",
                "target_code": code,
                "sample_rate": 48000,
                "reference_rms_db": -30.0,
                "input_scale": SCALE,
                "training_step": int(ck.get("step", -1)),
                "note": "TCN + per-pair EQ; trained on clean DI-like guitar recordings, level-normalised per cell",
            },
            "config": {"channels": ch, "kernel": kernel, "dilations": dilations, "blocks": n_blocks,
                       "bands": N_BANDS, "band_centers_hz": [round(float(f), 3) for f in BAND_CENTERS]},
            "targets": [{"position": where[i][1], "cell": cells[i]} for i in targets],
            "sources": [{"code": where[i][0], "position": where[i][1], "cell": cells[i]} for i in sources],
            "network": net,
            "conditioning": cond,  # [target][source] -> {film: [block][4*ch], eq_db: [bands]}
        }
        path = args.out_dir / f"4real-audio-{code}.ngm"
        path.write_text(json.dumps(doc, separators=(",", ":")), encoding="utf-8")

        # golden vectors: 3 (source, target) pairs, full torch output and the signal after the EQ
        golden = {"input": [float(v) for v in probe], "cases": []}
        pairs = [(sources[0], targets[0]), (sources[len(sources) // 2], targets[-1]), (sources[-1], targets[0])]
        with torch.no_grad():
            for si, ti in pairs:
                x = torch.from_numpy(probe)[None, None]
                s, tg = torch.tensor([si]), torch.tensor([ti])
                z = model.apply_eq(x, s, tg)
                y = model(x, s, tg)
                golden["cases"].append({"source": sources.index(si), "target": targets.index(ti),
                                        "eq_out": [float(v) for v in z[0, 0]], "out": [float(v) for v in y[0, 0]]})
        (args.out_dir / f"golden_{code}.json").write_text(json.dumps(golden, separators=(",", ":")), encoding="utf-8")
        print(f"{path.name}: {path.stat().st_size / 1024:.0f} KB, {len(targets)} target positions x {len(sources)} sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
