"""Turn frequency responses into real-time-friendly filters, and estimate a transfer curve
between two recordings that do NOT have to be the same performance (unpaired)."""
from __future__ import annotations

import numpy as np
from scipy.signal import firwin2, lfilter, minimum_phase, welch


def correction_db(source_response, target_response, max_boost_db: float = 12.0,
                  max_cut_db: float = -30.0) -> np.ndarray:
    """dB curve that turns the source guitar's response into the target's."""
    src = 20.0 * np.log10(np.maximum(np.abs(source_response), 1e-12))
    tgt = 20.0 * np.log10(np.maximum(np.abs(target_response), 1e-12))
    return np.clip(tgt - src, max_cut_db, max_boost_db)


def design_min_phase_fir(freq_hz, gain_db, fs: float, numtaps: int = 1023) -> np.ndarray:
    """Minimum-phase FIR (no pre-ringing) matching gain_db at freq_hz. Needs scipy >= 1.14."""
    freq_hz = np.asarray(freq_hz, dtype=float)
    gain_db = np.asarray(gain_db, dtype=float)
    nyq = fs / 2.0
    inner = freq_hz[(freq_hz > 0.0) & (freq_hz < nyq)]
    grid = np.concatenate(([0.0], inner, [nyq]))
    lin = 10.0 ** (np.interp(grid, freq_hz, gain_db) / 20.0)
    h_linear = firwin2(numtaps, grid / nyq, lin)
    return minimum_phase(h_linear, method="homomorphic", half=False)


def apply_fir(x: np.ndarray, h: np.ndarray) -> np.ndarray:
    return lfilter(h, [1.0], x, axis=0)


def smooth_octave(freqs, power, fraction: float = 1.0 / 3.0) -> np.ndarray:
    """Average power over a +/- fraction/2 octave window around each bin."""
    freqs = np.asarray(freqs, dtype=float)
    csum = np.concatenate(([0.0], np.cumsum(power)))
    lo = np.searchsorted(freqs, freqs * 2.0 ** (-fraction / 2.0), side="left")
    hi = np.searchsorted(freqs, freqs * 2.0 ** (fraction / 2.0), side="right")
    hi = np.maximum(hi, lo + 1)
    return (csum[hi] - csum[lo]) / (hi - lo)


def estimate_transfer(x_source: np.ndarray, x_target: np.ndarray, fs: float,
                      nperseg: int = 16384, fraction: float = 1.0 / 3.0):
    """Long-term spectrum ratio target/source in dB.

    The two recordings may be different performances. They must use similar material
    (for example the same scale or riff played in the same way) and long enough
    durations (a minute or more) so the averages are stable. Returns (freqs, ratio_db).
    """
    freqs, p_src = welch(x_source, fs, nperseg=nperseg)
    _, p_tgt = welch(x_target, fs, nperseg=nperseg)
    p_src = smooth_octave(freqs, p_src, fraction)
    p_tgt = smooth_octave(freqs, p_tgt, fraction)
    ratio = 10.0 * np.log10(np.maximum(p_tgt, 1e-20) / np.maximum(p_src, 1e-20))
    return freqs, ratio
