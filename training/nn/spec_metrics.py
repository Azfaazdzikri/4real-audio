"""Spectral-shape metric v2 ("wide"): numpy only.

The first metric (metrics.py) ignored every band more than 45 dB below the loudest band and everything above 8 kHz.
A guitar DI has little energy up there, so the model never saw the 'air' that separates a single coil from a humbucker, and
the learned EQ stopped dead at ~5 kHz. This version keeps a band if BOTH recordings are clearly above their own recording
noise floor, from 100 Hz up to 16 kHz.
"""
import numpy as np

from metrics import smooth_bands
from spec_common import CENTERS

MARGIN_DB = 12.0   # a band counts when it is this far above the cell's noise floor
FREQ_OK = (CENTERS >= 100.0) & (CENTERS <= 16000.0)


def valid_bands(src_clip, tgt_clip, floor_src, floor_tgt):
    return (np.asarray(src_clip) > np.asarray(floor_src) + MARGIN_DB) & \
           (np.asarray(tgt_clip) > np.asarray(floor_tgt) + MARGIN_DB) & FREQ_OK


def diff_wide(src_clip, tgt_clip, floor_src, floor_tgt):
    """tgt - src per band (smoothed), NaN where a band is not valid."""
    ok = valid_bands(src_clip, tgt_clip, floor_src, floor_tgt)
    return np.where(ok, smooth_bands(tgt_clip) - smooth_bands(src_clip), np.nan)


def rms_wide(src_clip, tgt_clip, pred_clip, floor_src, floor_tgt):
    """RMS (dB) between the true and predicted difference curves, each with its mean level removed."""
    ok = valid_bands(src_clip, tgt_clip, floor_src, floor_tgt)
    if ok.sum() < 15:
        return np.nan
    s, t, p = smooth_bands(src_clip), smooth_bands(tgt_clip), smooth_bands(pred_clip)
    true, est = (t - s)[ok], (p - s)[ok]
    r = (true - true.mean()) - (est - est.mean())
    return float(np.sqrt(np.mean(r ** 2)))


def rms_curve(true_d, pred_d):
    """Same, for ready-made difference curves (NaN = ignore)."""
    ok = ~np.isnan(true_d) & ~np.isnan(pred_d)
    if ok.sum() < 15:
        return np.nan
    r = (true_d[ok] - true_d[ok].mean()) - (pred_d[ok] - pred_d[ok].mean())
    return float(np.sqrt(np.mean(r ** 2)))


def fill_nan(curve):
    """Replace NaN by the nearest valid value (edges held); zeros if nothing is valid."""
    curve = np.asarray(curve, dtype=np.float64)
    idx = np.flatnonzero(~np.isnan(curve))
    if idx.size == 0:
        return np.zeros_like(curve)
    return np.interp(np.arange(len(curve)), idx, curve[idx])
