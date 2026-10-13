"""Spectral-shape metrics used to compare predictions with real recordings. numpy only."""
import numpy as np

BAND_CENTERS = np.sqrt(np.geomspace(70.0, 19000.0, 65)[:-1] * np.geomspace(70.0, 19000.0, 65)[1:])
RANGE = (BAND_CENTERS >= 150) & (BAND_CENTERS <= 8000)


def smooth_bands(bands_db):
    """3-band moving average in the power domain (about 0.4 octave)."""
    p = 10.0 ** (np.asarray(bands_db, dtype=np.float64) / 10.0)
    k = np.ones(3) / 3.0
    return 10.0 * np.log10(np.convolve(np.pad(p, (1, 1), mode="edge"), k, mode="valid") + 1e-14)


def diff(src_db, tgt_db):
    """Per-band difference tgt - src, NaN where either band is more than 45 dB below its peak or out of range."""
    a, b = smooth_bands(src_db), smooth_bands(tgt_db)
    ok = (a > a.max() - 45) & (b > b.max() - 45) & RANGE
    return np.where(ok, b - a, np.nan)


def rms_shape(true_diff, predicted_diff):
    """RMS (dB) between two difference curves after removing each one's mean level."""
    ok = ~np.isnan(true_diff) & ~np.isnan(predicted_diff)
    if ok.sum() < 15:
        return np.nan
    r = (true_diff[ok] - true_diff[ok].mean()) - (predicted_diff[ok] - predicted_diff[ok].mean())
    return float(np.sqrt(np.mean(r ** 2)))
