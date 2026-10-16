"""Shared definitions for the spectral-envelope model (v3). numpy only.

Analysis frame: 4096 samples (85 ms) Hann window, hop 512 (10.7 ms), at 48 kHz. Each frame is reduced to the
same 64 log-spaced bands (70 Hz - 19 kHz) used by the evaluation metric. The model predicts, for every frame,
a gain curve (dB per band) that is applied to the input as a minimum-phase FIR filter: the output is always
the input filtered, so it cannot add distortion or noise (unlike the waveform-generating v1/v2 networks).
"""
import numpy as np

RATE = 48000
NFFT = 4096
HOP = 512
N_BANDS = 64
EDGES = np.geomspace(70.0, 19000.0, N_BANDS + 1)
CENTERS = np.sqrt(EDGES[:-1] * EDGES[1:])
RANGE = (CENTERS >= 150) & (CENTERS <= 8000)
WINDOW = np.hanning(NFFT + 1)[:-1]  # periodic Hann (same as torch.hann_window)
PAD_DB = -140.0                     # value of frames beyond the end of a clip

_W = None


def band_matrix():
    """(NFFT/2+1, 64): power-spectrum bins -> band means. Bands narrower than one bin use the nearest bin."""
    global _W
    if _W is None:
        freqs = np.fft.rfftfreq(NFFT, 1.0 / RATE)
        W = np.zeros((len(freqs), N_BANDS), dtype=np.float32)
        for i in range(N_BANDS):
            sel = (freqs >= EDGES[i]) & (freqs < EDGES[i + 1])
            if not sel.any():
                sel = np.zeros(len(freqs), dtype=bool)
                sel[int(np.argmin(np.abs(freqs - CENTERS[i])))] = True
            W[sel, i] = 1.0 / sel.sum()
        _W = W
    return _W


def num_frames(n_samples):
    return 0 if n_samples < NFFT else 1 + (n_samples - NFFT) // HOP


def frame_bands_db(x):
    """x: mono float signal -> (frames, 64) band power in dB (float32). Frame f covers samples [f*HOP, f*HOP+NFFT)."""
    x = np.asarray(x, dtype=np.float32)
    nf = num_frames(len(x))
    if nf == 0:
        return np.zeros((0, N_BANDS), dtype=np.float32)
    out = np.empty((nf, N_BANDS), dtype=np.float32)
    W = band_matrix()
    norm = float(np.sum(WINDOW ** 2))
    step = 128  # frames per chunk, keeps memory small
    for a in range(0, nf, step):
        b = min(nf, a + step)
        idx = np.arange(NFFT)[None, :] + HOP * np.arange(a, b)[:, None]
        X = np.fft.rfft(x[idx] * WINDOW[None, :].astype(np.float32), axis=1)
        P = (X.real.astype(np.float32) ** 2 + X.imag.astype(np.float32) ** 2) / norm
        out[a:b] = 10.0 * np.log10(P @ W + 1e-14)
    return out


def clip_db(frames_db):
    """Time-averaged band spectrum (dB) of frames (n, 64): mean power, then dB."""
    p = np.mean(10.0 ** (np.asarray(frames_db, dtype=np.float64) / 10.0), axis=0)
    return 10.0 * np.log10(p + 1e-14)


def band_gain_to_bins(gain_db, n_fft, rate=RATE):
    """64-band gain curve (dB) -> gain per FFT bin: linear in Hz between band centres, edge values held."""
    f = np.fft.rfftfreq(n_fft, 1.0 / rate)
    return np.interp(f, CENTERS, gain_db)


def min_phase_fir(gain_db, taps=1024, n_fft=8192, rate=RATE):
    """Minimum-phase FIR (taps) whose magnitude follows the 64-band gain curve (cepstral method)."""
    mag_db = band_gain_to_bins(gain_db, n_fft, rate)
    log_mag = mag_db / 20.0 * np.log(10.0)
    full = np.concatenate([log_mag, log_mag[-2:0:-1]])           # even-symmetric log magnitude
    cep = np.fft.ifft(full).real
    fold = np.zeros_like(cep)
    fold[0] = cep[0]
    fold[1:n_fft // 2] = 2.0 * cep[1:n_fft // 2]
    fold[n_fft // 2] = cep[n_fft // 2]
    h = np.fft.ifft(np.exp(np.fft.fft(fold))).real[:taps]
    fade = min(64, taps // 4)
    w = np.ones(taps)
    w[taps - fade:] = 0.5 * (1.0 - np.cos(np.pi * np.arange(fade - 1, -1, -1) / fade))  # same shape as the C++ designer
    return (h * w).astype(np.float32)
