"""numpy re-implementation of the SpecEQ network + the exact run-time algorithm of the plugin (reference for the C++ port).

Run-time algorithm (what the plugin does, per hop of 512 samples, mono):
  1. keep the last 4096 input samples; at every hop boundary compute the 64-band spectrum of that window (a new frame f)
  2. net output for frame f (needs frames <= f) = dynamic correction curve; gain for the next hop = static EQ[src, tgt] + that curve
  3. gain curve -> minimum-phase FIR (1024 taps); the output of the hop is a linear crossfade of the old and new filter
Frame f covers samples [f*512, f*512 + 4096), so it is complete at the start of hop f + 8: hop j uses frame j - 8.
"""
import math
import numpy as np

from spec_common import HOP, N_BANDS, NFFT, RANGE, frame_bands_db, min_phase_fir

HOPS_PER_FRAME = NFFT // HOP  # 8


def _silu(x):
    return x / (1.0 + np.exp(-x))


def _sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


class SpecNumpy:
    """Weights from a model.pt state dict (any array-likes) + its meta dict."""

    def __init__(self, state, meta):
        s = {k: np.asarray(v, dtype=np.float64) for k, v in state.items()}
        self.s, self.meta = s, meta
        self.cells = meta["cells"]
        self.ch = meta["ch"]
        self.dilations = meta["dilations"]
        self.max_delta = meta["max_delta_db"]
        self.eq_limit = meta["eq_limit_db"]
        self.n_ctrl = meta["n_ctrl"]
        self.range = RANGE.astype(np.float64)
        self.ctrl_w = s["ctrl_w"]

    def zero_mean(self, g):
        return g - (g * self.range).sum(-1, keepdims=True) / self.range.sum()

    def static_eq(self, src, tgt):
        return self.zero_mean(np.clip(self.s["eq"][src, tgt], -self.eq_limit, self.eq_limit))

    def features(self, L):
        shape = np.clip(self.zero_mean(L), -60.0, 30.0) / 20.0
        level = 10.0 * np.log10((10.0 ** (L / 10.0)).sum(-1, keepdims=True) + 1e-14)
        level = np.clip((level + 30.0) / 30.0, -3.0, 2.0)
        return np.concatenate([shape, level], axis=-1).T  # (65, F)

    def conv(self, x, w, b, dilation):
        """causal dilated conv: x (Cin, F), w (Cout, Cin, K)."""
        k = w.shape[2]
        pad = (k - 1) * dilation
        xp = np.pad(x, ((0, 0), (pad, 0)))
        n = x.shape[1]
        out = np.zeros((w.shape[0], n)) + b[:, None]
        for t in range(k):
            out += w[:, :, t] @ xp[:, t * dilation: t * dilation + n]
        return out

    def delta(self, L, src, tgt):
        """Dynamic correction for every frame, computed from frames <= f (no lag): (F, 64) dB."""
        s = self.s
        e = np.concatenate([s["emb.weight"][src], s["emb.weight"][tgt]])
        c = _silu(s["cond.0.weight"] @ e + s["cond.0.bias"])
        h = self.conv(self.features(L), s["inp.weight"], s["inp.bias"], 1)
        skip = 0.0
        for i, d in enumerate(self.dilations):
            p = f"blocks.{i}."
            z = self.conv(h, s[p + "conv.weight"], s[p + "conv.bias"], d)
            film = s[p + "film.weight"] @ c + s[p + "film.bias"]
            gamma, beta = film[:2 * self.ch], film[2 * self.ch:]
            z = z * (1.0 + gamma[:, None]) + beta[:, None]
            a, b = z[:self.ch], z[self.ch:]
            z = np.tanh(a) * _sigmoid(b)
            h = h + s[p + "res.weight"][:, :, 0] @ z + s[p + "res.bias"][:, None]
            skip = skip + s[p + "skip.weight"][:, :, 0] @ z + s[p + "skip.bias"][:, None]
        out = s["head.1.weight"][:, :, 0] @ _silu(skip) + s["head.1.bias"][:, None]  # (n_ctrl, F)
        out = self.max_delta * np.tanh(out / self.max_delta)
        return self.zero_mean(out.T @ self.ctrl_w)

    def hop_gains(self, L, src, tgt, n_hops, dynamic=True):
        """Gain curve (dB) used for every hop j of the plugin: static + delta of frame j - 8 (0 before frame 0)."""
        static = self.static_eq(src, tgt)
        d = self.delta(L, src, tgt) if (dynamic and len(L)) else np.zeros((len(L), N_BANDS))
        gains = np.empty((n_hops, N_BANDS))
        for j in range(n_hops):
            f = j - HOPS_PER_FRAME
            gains[j] = static + (d[f] if 0 <= f < len(d) else 0.0)
        return gains * getattr(self, 'intensity', 1.0)


def apply_hop_filters(x, gains, taps=1024):
    """Time-varying min-phase FIR, one filter per hop with a linear crossfade from the previous filter."""
    n_hops = len(gains)
    xp = np.concatenate([np.zeros(taps - 1), np.asarray(x, dtype=np.float64)])
    y = np.zeros(n_hops * HOP)
    prev = None
    ramp = (np.arange(HOP) + 1.0) / HOP
    for j in range(n_hops):
        h = min_phase_fir(gains[j], taps).astype(np.float64)
        seg = xp[j * HOP: j * HOP + HOP + taps - 1]
        new = np.convolve(seg, h, mode="valid")
        if prev is None:
            y[j * HOP:(j + 1) * HOP] = new
        else:
            old = np.convolve(seg, prev, mode="valid")
            y[j * HOP:(j + 1) * HOP] = old + ramp * (new - old)
        prev = h
    return y


def process(model, x, src, tgt, gain_db, dynamic=True):
    """x: raw mono audio. gain_db: level normalisation of the source cell (analysis only). Returns (filtered, hop gains)."""
    L = frame_bands_db(x * 10.0 ** (gain_db / 20.0))
    n_hops = len(x) // HOP
    gains = model.hop_gains(L, src, tgt, n_hops, dynamic)
    return apply_hop_filters(x[:n_hops * HOP], gains), gains
