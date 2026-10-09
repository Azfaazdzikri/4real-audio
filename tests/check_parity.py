"""Checks that the C++ circuit model (Source/dsp) matches the Python model (training/).

    g++ -std=c++17 -O2 -ISource/dsp Source/dsp/GuitarCircuit.cpp Source/dsp/FilterDesign.cpp tests/dsp_test.cpp -o dsp_test
    python tests/check_parity.py ./dsp_test
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "training"))

from guitar_circuit import LesPaulCircuit, Rig, les_paul, strat_hss, strat_sss  # noqa: E402

FREQS = np.array([20, 50, 100, 300, 1000, 2000, 3000, 4000, 6000, 10000, 20000], dtype=float)
db = lambda h: 20 * np.log10(np.abs(h))

CASES = {
    "hss_pos1": lambda: strat_hss().response(FREQS, position=1, volume=10, tone_neck=10, tone_mid=10),
    "hss_pos2_v7_tm3": lambda: strat_hss().response(FREQS, position=2, volume=7, tone_neck=10, tone_mid=3),
    "sss_pos5_tn0": lambda: strat_sss().response(FREQS, position=5, volume=10, tone_neck=0, tone_mid=10),
    "sss_pos4_v5_tn4_tm8": lambda: strat_sss().response(FREQS, position=4, volume=5, tone_neck=4, tone_mid=8),
    "lp_bridge": lambda: les_paul().response(FREQS, selector="bridge"),
    "lp_both_mixed": lambda: les_paul().response(FREQS, selector="both", vol_neck=6, vol_bridge=9,
                                                   tone_neck=2, tone_bridge=7),
    "lp_neck_tn0": lambda: les_paul().response(FREQS, selector="neck", tone_neck=0),
    "lp_bridge_cable200": lambda: les_paul().response(FREQS, rig=Rig(cable_pf=200), selector="bridge"),
}


def main(binary: str) -> int:
    out = subprocess.run([binary], capture_output=True, text=True).stdout
    got = {}
    for line in out.splitlines():
        if line.startswith("RESP "):
            _, name, f, v = line.split()
            got.setdefault(name, []).append(float(v))
    worst = 0.0
    for name, make in CASES.items():
        want = db(make())
        have = np.array(got[name])
        err = float(np.max(np.abs(want - have)))
        worst = max(worst, err)
        print(f"{name:24s} max |C++ - Python| = {err:.2e} dB")
    ok = worst < 0.01
    print("PARITY OK" if ok else "PARITY FAILED", f"(worst {worst:.2e} dB)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
