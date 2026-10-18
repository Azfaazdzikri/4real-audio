"""Export a trained v3 model.pt to .spm files (spectral model): one file per TARGET guitar. numpy only (no torch needed).

    python training/nn/export_spec.py model.pt models/

A .spm file is JSON: architecture, the shared network weights, band layout, and for every (target position, source cell)
pair the conditioning that is already worked out (FiLM vectors per block + the static EQ curve in dB), so the plugin needs
no embedding tables. Also writes golden test vectors (golden_<CODE>.json) made with the numpy reference (spec_numpy.py),
so the C++ engine can be checked against exactly the algorithm that was measured in training and listening.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from guitar_map import GUITARS, guitar_of, position_of  # noqa: E402  (cell name -> plugin guitar / position)
from spec_common import CENTERS, EDGES, HOP, N_BANDS, NFFT, RANGE, frame_bands_db  # noqa: E402
from spec_numpy import AutoSource, SpecNumpy, _silu, apply_hop_filters, process  # noqa: E402
from torchless import load_pt  # noqa: E402

FORMAT_VERSION = "0.2.0"
AUTO_MASK = (CENTERS >= 150.0) & (CENTERS <= 12000.0)   # bands that identify the source (wide features of auto_source.py)
AUTO_TAU_DB, AUTO_GATE_DB, AUTO_SECONDS, AUTO_MIN_FRAMES = 0.5, -50.0, 1.0, 4


def fmt(a, digits=7):
    return [float(f"{v:.{digits}g}") for v in np.asarray(a, dtype=np.float32).ravel()]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("model", type=Path)
    ap.add_argument("out_dir", type=Path, help="models/ (writes <CODE>/model.spm and tests/golden alongside via --golden-dir)")
    ap.add_argument("--golden-dir", type=Path, default=None)
    ap.add_argument("--protos", type=Path, default=None,
                    help="prototypes.npz from auto_source.py (spectrum shape of every cell). Without it the automatic source is "
                         "written with PLACEHOLDER prototypes and disabled (enabled=false); tests still use it.")
    args = ap.parse_args()
    if N_BANDS != 64:
        raise SystemExit("export_spec supports the 64-band v3 model (unset FOURREAL_BANDS)")

    ck = load_pt(args.model)
    state, cells = ck["model"], ck["cells"]
    if "bconv1.weight" in state:
        raise SystemExit("this checkpoint is a v4 (fine band) model: not supported by the plugin")
    ref = SpecNumpy(state, ck)
    ch, dil = int(ck["ch"]), [int(d) for d in ck["dilations"]]

    where = {}
    for i, cell in enumerate(cells):
        code, pos = guitar_of(cell), position_of(cell)
        where[i] = (code, dict(GUITARS)[code].index(pos))
    assert len(set(where.values())) == len(cells) == 17
    sources = sorted(range(len(cells)), key=lambda i: ([g for g, _ in GUITARS].index(where[i][0]), where[i][1]))

    weights = {"inp.weight": state["inp.weight"], "inp.bias": state["inp.bias"]}
    for i in range(len(dil)):
        for nm in ("conv", "res", "skip"):
            weights[f"blocks.{i}.{nm}.weight"] = state[f"blocks.{i}.{nm}.weight"]
            weights[f"blocks.{i}.{nm}.bias"] = state[f"blocks.{i}.{nm}.bias"]
    weights["head.weight"] = state["head.1.weight"]
    weights["head.bias"] = state["head.1.bias"]
    net = {k: {"shape": list(np.shape(v)), "data": fmt(v)} for k, v in weights.items()}

    # --- automatic source: prototypes (cells order of the checkpoint) ---
    n_mask = int(AUTO_MASK.sum())
    auto_real = args.protos is not None
    if auto_real:
        z = np.load(args.protos, allow_pickle=True)
        names = [str(c) for c in z["cells"]]
        assert sorted(names) == sorted(cells), "prototypes.npz was made for different cells"
        protos_cells = np.stack([z["wide"][names.index(c)] for c in cells]).astype(np.float64)
        assert protos_cells.shape[1] == n_mask, f"prototypes have {protos_cells.shape[1]} bands, expected {n_mask}"
    protos_cells = protos_cells if auto_real else None   # placeholder is made from the probe below

    # probe signal for the golden vectors: harmonic decaying note + a second note + noise, 1.5 s
    rng = np.random.default_rng(1234)
    t = np.arange(43200) / 48000.0   # 0.9 s = 84 hops
    probe = (0.03 * sum(np.sin(2 * np.pi * 110.0 * n * t) / n for n in range(1, 9)) * np.exp(-1.5 * t)
             + 0.02 * np.sin(2 * np.pi * 330.0 * t) * np.exp(-0.8 * np.maximum(t - 0.4, 0)) * (t > 0.4)
             + 0.002 * rng.standard_normal(len(t))).astype(np.float32)
    probe = probe.astype(np.float64)
    # the same note with a silent gap (0.50 - 0.65 s) to exercise the loudness gate of the automatic source
    probe_auto = probe.copy()
    probe_auto[24000:31200] = 1e-4 * rng.standard_normal(7200)
    if not auto_real:
        from spec_numpy import smooth3
        Lp = frame_bands_db(probe_auto)
        feat = smooth3(10.0 * np.log10(np.mean(10.0 ** (Lp.astype(np.float64) / 10.0), axis=0) + 1e-14))[AUTO_MASK]
        feat = feat - feat.mean()
        protos_cells = feat[None, :] + rng.normal(0.0, 1.5, (len(cells), n_mask))   # placeholder: distances of a few dB
    auto_src = AutoSource(protos_cells, AUTO_MASK, AUTO_TAU_DB, AUTO_GATE_DB, AUTO_SECONDS, AUTO_MIN_FRAMES)

    for code, order in GUITARS:
        targets = sorted([i for i in range(len(cells)) if where[i][0] == code], key=lambda i: where[i][1])
        cond = []
        for ti in targets:
            row = []
            for si in sources:
                e = np.concatenate([state["emb.weight"][si], state["emb.weight"][ti]]).astype(np.float64)
                c = _silu(state["cond.0.weight"].astype(np.float64) @ e + state["cond.0.bias"])
                film = [fmt(state[f"blocks.{i}.film.weight"].astype(np.float64) @ c + state[f"blocks.{i}.film.bias"])
                        for i in range(len(dil))]
                row.append({"film": film, "eq_db": fmt(ref.static_eq(si, ti))})
            cond.append(row)
        doc = {
            "format": "spm", "version": FORMAT_VERSION,
            "metadata": {"name": f"4real-audio {code}", "target_code": code, "sample_rate": 48000, "reference_rms_db": -30.0,
                         "training_step": int(ck.get("step", -1)),
                         "note": "spectral-envelope model: static per-pair EQ + dynamic correction, applied as a min-phase FIR"},
            "config": {"channels": ch, "kernel": 3, "dilations": dil, "bands": N_BANDS, "n_ctrl": int(ck["n_ctrl"]),
                       "nfft": NFFT, "hop": HOP, "max_delta_db": float(ck["max_delta_db"]), "eq_limit_db": float(ck["eq_limit_db"]),
                       "range_lo_hz": 150.0, "range_hi_hz": 8000.0, "taps": 1024, "design_fft": 8192,
                       "band_edges_hz": [float(f"{v:.9g}") for v in EDGES]},
            "targets": [{"position": where[i][1], "cell": cells[i]} for i in targets],
            "sources": [{"code": where[i][0], "position": where[i][1], "cell": cells[i]} for i in sources],
            "network": net,
            "conditioning": cond,
            "auto": {
                "enabled": bool(auto_real), "tau_db": AUTO_TAU_DB, "gate_db": AUTO_GATE_DB, "avg_seconds": AUTO_SECONDS,
                "min_frames": AUTO_MIN_FRAMES, "mask": [int(v) for v in AUTO_MASK],
                "protos": {"shape": [len(sources), n_mask], "data": fmt(protos_cells[sources])},
                "emb_src": {"shape": [len(sources), state["emb.weight"].shape[1]], "data": fmt(state["emb.weight"][sources])},
                "emb_tgt": {"shape": [len(targets), state["emb.weight"].shape[1]], "data": fmt(state["emb.weight"][targets])},
                "cond_w": {"shape": list(state["cond.0.weight"].shape), "data": fmt(state["cond.0.weight"])},
                "cond_b": {"shape": list(state["cond.0.bias"].shape), "data": fmt(state["cond.0.bias"])},
                "film_w": [{"shape": list(state[f"blocks.{i}.film.weight"].shape), "data": fmt(state[f"blocks.{i}.film.weight"])}
                           for i in range(len(dil))],
                "film_b": [{"shape": list(state[f"blocks.{i}.film.bias"].shape), "data": fmt(state[f"blocks.{i}.film.bias"])}
                           for i in range(len(dil))],
            },
        }
        out = args.out_dir / code
        out.mkdir(parents=True, exist_ok=True)
        path = out / "model.spm"
        path.write_text(json.dumps(doc, separators=(",", ":")), encoding="utf-8")

        if code not in ("LP", "STR", "FLC"):
            print(f"{path}: {path.stat().st_size / 1024:.0f} KB, {len(targets)} target positions x {len(sources)} sources")
            continue
        golden = {"input": fmt(probe, 7), "cases": []}
        for n_case, (si, ti) in enumerate([(sources[0], targets[0]), (sources[len(sources) // 2], targets[-1]), (sources[-1], targets[0])]):
            ref.intensity = 1.0
            y, gains = process(ref, probe, si, ti, 0.0, dynamic=True)
            keep = [0, 7, 8, 9, 30, 60, 80]
            case = {"source": sources.index(si), "target": targets.index(ti), "out": fmt(y, 7),
                    "gain_hops": keep, "gains": [fmt(gains[j], 8) for j in keep]}
            if n_case == 0:   # the intensity knob (gains multiplied before the filter design)
                ref.intensity = 1.5
                case["out_x1_5"] = fmt(process(ref, probe, si, ti, 0.0, dynamic=True)[0], 7)
                ref.intensity = 1.0
            golden["cases"].append(case)
        # automatic source: weights of selected hops, gain curves and the filtered audio (target = last position)
        ti = targets[-1]
        ref.intensity = 1.0
        L_auto = frame_bands_db(probe_auto)
        W = auto_src.run(L_auto)                                                  # (frames, cells), cells order
        n_hops = len(probe_auto) // HOP
        gains_auto = ref.hop_gains_auto(L_auto, W, ti, n_hops)
        y_auto = apply_hop_filters(probe_auto[:n_hops * HOP], gains_auto)
        keep = [0, 7, 8, 9, 20, 30, 45, 60, 80]
        uniform = np.full(len(cells), 1.0 / len(cells))
        golden["input_auto"] = fmt(probe_auto, 7)
        golden["auto"] = {"target": targets.index(ti), "out": fmt(y_auto, 7), "hops": keep,
                          "weights": [fmt((W[j - 8] if j >= 8 else uniform)[sources], 8) for j in keep],
                          "gains": [fmt(gains_auto[j], 8) for j in keep]}
        gdir = args.golden_dir or (args.out_dir.parent / "tests" / "golden")
        gdir.mkdir(parents=True, exist_ok=True)
        (gdir / f"golden_spm_{code}.json").write_text(json.dumps(golden, separators=(",", ":")), encoding="utf-8")
        print(f"{path}: {path.stat().st_size / 1024:.0f} KB, {len(targets)} target positions x {len(sources)} sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
