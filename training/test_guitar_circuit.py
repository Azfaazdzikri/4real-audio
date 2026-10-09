"""Run:  python -m unittest training.test_guitar_circuit -v   (from the repo root)
or:    python training/test_guitar_circuit.py"""
import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from guitar_circuit import Rig, les_paul, parse_spec, strat_hss, strat_sss  # noqa: E402
from transfer import (apply_fir, correction_db, design_min_phase_fir,  # noqa: E402
                      estimate_transfer)

FREQS = np.geomspace(20.0, 20000.0, 800)


def db(h):
    return 20.0 * np.log10(np.abs(h))


def peak_hz(h):
    return FREQS[int(np.argmax(np.abs(h)))]


def db_at(h, f):
    return float(np.interp(np.log(f), np.log(FREQS), db(h)))


class CircuitTests(unittest.TestCase):
    def test_single_coil_resonance_in_expected_range(self):
        h = strat_sss().response(FREQS, position=5)
        self.assertTrue(3000 < peak_hz(h) < 7000, peak_hz(h))

    def test_humbucker_resonates_lower_than_single_coil(self):
        sc = peak_hz(strat_sss().response(FREQS, position=5))
        hb = peak_hz(les_paul().response(FREQS, selector="bridge"))
        self.assertLess(hb, sc - 1000.0)

    def test_tone_knob_cuts_highs_not_lows(self):
        g = strat_sss()
        full = g.response(FREQS, position=5, tone_neck=10)
        closed = g.response(FREQS, position=5, tone_neck=0)
        self.assertLess(db_at(closed, 5000), db_at(full, 5000) - 6.0)
        self.assertLess(abs(db_at(closed, 100) - db_at(full, 100)), 1.5)

    def test_volume_knob_is_monotonic_and_reaches_silence(self):
        g = strat_sss()
        levels = [db_at(g.response(FREQS, position=1, volume=v), 1000) for v in (0, 2, 5, 8, 10)]
        self.assertEqual(levels, sorted(levels))
        self.assertLess(levels[0], levels[-1] - 60.0)

    def test_parallel_pickups_resonate_higher(self):
        g = strat_sss()
        single = peak_hz(g.response(FREQS, position=1))
        parallel = peak_hz(g.response(FREQS, position=2))
        self.assertGreater(parallel, single)

    def test_les_paul_both_is_higher_than_bridge_alone(self):
        g = les_paul()
        self.assertGreater(peak_hz(g.response(FREQS, selector="both")),
                           peak_hz(g.response(FREQS, selector="bridge")))

    def test_les_paul_pickups_are_independent(self):
        g = les_paul()
        a = g.response(FREQS, selector="bridge", vol_neck=0, tone_neck=0)
        b = g.response(FREQS, selector="bridge", vol_neck=10, tone_neck=10)
        np.testing.assert_allclose(np.abs(a), np.abs(b), rtol=1e-9)

    def test_longer_cable_lowers_resonance(self):
        g = strat_sss()
        short = peak_hz(g.response(FREQS, rig=Rig(cable_pf=200), position=5))
        long_ = peak_hz(g.response(FREQS, rig=Rig(cable_pf=1000), position=5))
        self.assertLess(long_, short)

    def test_hss_bridge_humbucker_is_hotter_than_single_coil(self):
        hss = db_at(strat_hss().response(FREQS, position=1), 1000)
        sss = db_at(strat_sss().response(FREQS, position=1), 1000)
        self.assertGreater(hss, sss)

    def test_parse_spec(self):
        g, c = parse_spec("lp:selector=bridge,vol_bridge=7")
        self.assertEqual(c, {"selector": "bridge", "vol_bridge": 7.0})
        self.assertEqual(g.name, "lespaul")
        with self.assertRaises(ValueError):
            parse_spec("nope")


class DspTests(unittest.TestCase):
    fs = 48000.0

    def test_min_phase_fir_matches_target_magnitude(self):
        freqs = np.geomspace(20, 20000, 400)
        target = 6.0 * np.exp(-((np.log(freqs) - np.log(3000.0)) ** 2) / 0.8) - 3.0
        h = design_min_phase_fir(freqs, target, self.fs)
        w = np.fft.rfftfreq(8192, 1 / self.fs)
        resp = 20 * np.log10(np.abs(np.fft.rfft(h, 8192)) + 1e-12)
        sel = (w > 100) & (w < 12000)
        want = np.interp(w[sel], freqs, target)
        self.assertLess(float(np.max(np.abs(resp[sel] - want))), 1.0)
        # causal, energy at the start (no pre-ringing)
        self.assertGreater(np.sum(h[:64] ** 2), 0.95 * np.sum(h ** 2))

    def test_unpaired_estimation_recovers_model_difference(self):
        """Different noise realisations stand in for two different performances."""
        src, tgt = strat_sss(), les_paul()
        src_resp = src.response(FREQS, position=5)
        tgt_resp = tgt.response(FREQS, selector="bridge")
        n = int(self.fs * 90)
        rng_a, rng_b = np.random.default_rng(1), np.random.default_rng(2)
        # same 'string' spectrum, different performances, then each guitar's response
        h_src = design_min_phase_fir(FREQS, db(src_resp), self.fs)
        h_tgt = design_min_phase_fir(FREQS, db(tgt_resp), self.fs)
        x_src = apply_fir(rng_a.standard_normal(n), h_src)
        x_tgt = apply_fir(rng_b.standard_normal(n), h_tgt)
        f, est = estimate_transfer(x_src, x_tgt, self.fs)
        truth = np.interp(f, FREQS, db(tgt_resp) - db(src_resp))
        sel = (f > 200) & (f < 8000)
        err = float(np.mean(np.abs(est[sel] - truth[sel])))
        self.assertLess(err, 1.0, err)

    def test_correction_is_clipped(self):
        c = correction_db(np.array([1e-6]), np.array([1.0]), max_boost_db=12.0)
        self.assertEqual(float(c[0]), 12.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
