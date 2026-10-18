"""numpy re-implementation of the SpecEQ network + the exact run-time algorithm of the plugin (reference for the C++ port).

Run-time algorithm (what the plugin does, per hop of 512 samples, mono):
  1. keep the last 4096 input samples; at every hop boundary compute the band spectrum (64 bands v3, 162 bands v4) of that window (a new frame f)
  2. net output for frame f (needs frames <= f) = dynamic correction curve; gain for the next hop = static EQ[src, tgt] + that curve
  3. gain curve -> minimum-phase FIR (1024 taps); the output of the hop is a linear crossfade of the old and new filter
Frame f covers samples [f*512, f*512 + 4096), so it is complete at the start of hop f + 8: hop j uses frame j - 8.
"""
import math
import numpy as np

from spec_common import CENTERS, HOP, N_BANDS, NFFT, RANGE, clip_db, frame_bands_db, min_phase_fir

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

    def embed(self, L):
        """Input frames (F, N) -> (ch, F): v3 = 1x1 conv on the features, v4 = convolution across frequency + dense layer."""
        s = self.s
        if not self.meta.get("band_ch"):
            return self.conv(self.features(L), s["inp.weight"], s["inp.bias"], 1)
        shape = np.clip(self.zero_mean(L), -60.0, 30.0) / 20.0                        # (F, N)
        level = 10.0 * np.log10((10.0 ** (L / 10.0)).sum(-1, keepdims=True) + 1e-14)
        level = np.clip((level + 30.0) / 30.0, -3.0, 2.0)                             # (F, 1)
        h = _silu(self.freq_conv(shape[:, None, :], s["bconv1.weight"], s["bconv1.bias"], 1))
        h = _silu(self.freq_conv(h, s["bconv2.weight"], s["bconv2.bias"], 3))         # (F, bch, N)
        e = h.reshape(len(L), -1) @ s["binp.weight"].T + s["binp.bias"] + level @ s["linp.weight"].T + s["linp.bias"]
        return e.T                                                                     # (ch, F)

    @staticmethod
    def freq_conv(x, w, b, dilation):
        """'same' conv along the last axis: x (F, Cin, N), w (Cout, Cin, K) -> (F, Cout, N)."""
        k = w.shape[2]
        pad = (k - 1) // 2 * dilation
        xp = np.pad(x, ((0, 0), (0, 0), (pad, pad)))
        n = x.shape[2]
        out = np.zeros((x.shape[0], w.shape[0], n)) + b[None, :, None]
        for t in range(k):
            out += np.einsum("oc,fcn->fon", w[:, :, t], xp[:, :, t * dilation: t * dilation + n])
        return out

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
        """Dynamic correction for every frame, computed from frames <= f (no lag): (F, N_BANDS) dB."""
        s = self.s
        e = np.concatenate([s["emb.weight"][src], s["emb.weight"][tgt]])
        c = _silu(s["cond.0.weight"] @ e + s["cond.0.bias"])
        h = self.embed(L)
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


    # ------------------------------------------------------------------------------ automatic source (mixed source cells)
    def delta_auto(self, L, W, tgt):
        """Like delta(), but the source is a MIX that changes from frame to frame: W (F, cells) weights per frame.
        The mixed source embedding goes through the conditioning net of every frame separately (FiLM varies with time)."""
        s = self.s
        e_src = W @ s["emb.weight"]                                       # (F, emb)
        e = np.concatenate([e_src, np.tile(s["emb.weight"][tgt], (len(L), 1))], axis=1)
        c = _silu(e @ s["cond.0.weight"].T + s["cond.0.bias"])           # (F, cond)
        h = self.embed(L)
        skip = 0.0
        for i, d in enumerate(self.dilations):
            p = f"blocks.{i}."
            z = self.conv(h, s[p + "conv.weight"], s[p + "conv.bias"], d)
            film = (c @ s[p + "film.weight"].T + s[p + "film.bias"]).T   # (4ch, F)
            gamma, beta = film[:2 * self.ch], film[2 * self.ch:]
            z = z * (1.0 + gamma) + beta
            a, b = z[:self.ch], z[self.ch:]
            z = np.tanh(a) * _sigmoid(b)
            h = h + s[p + "res.weight"][:, :, 0] @ z + s[p + "res.bias"][:, None]
            skip = skip + s[p + "skip.weight"][:, :, 0] @ z + s[p + "skip.bias"][:, None]
        out = s["head.1.weight"][:, :, 0] @ _silu(skip) + s["head.1.bias"][:, None]
        out = self.max_delta * np.tanh(out / self.max_delta)
        return self.zero_mean(out.T @ self.ctrl_w)

    def hop_gains_auto(self, L, W, tgt, n_hops):
        """Gain curve of every hop j with the automatic source: weights and delta of frame j - 8 (uniform weights, static EQ only
        before frame 0). W (F, cells) are the per-frame weights from AutoSource.run()."""
        n_cells = len(self.cells)
        eq_all = np.stack([self.static_eq(a, tgt) for a in range(n_cells)])      # (cells, N)
        uniform = np.full(n_cells, 1.0 / n_cells)
        d = self.delta_auto(L, W, tgt) if len(L) else np.zeros((0, N_BANDS))
        gains = np.empty((n_hops, N_BANDS))
        for j in range(n_hops):
            f = j - HOPS_PER_FRAME
            if 0 <= f < len(d):
                gains[j] = W[f] @ eq_all + d[f]
            else:
                gains[j] = uniform @ eq_all
        return gains * getattr(self, 'intensity', 1.0)


def smooth3(bands_db):
    """3-band moving average in the power domain, edges repeated (same as metrics.smooth_bands)."""
    p = 10.0 ** (np.asarray(bands_db, dtype=np.float64) / 10.0)
    return 10.0 * np.log10(np.convolve(np.pad(p, (1, 1), mode="edge"), np.ones(3) / 3.0, mode="valid") + 1e-14)


class AutoSource:
    """What the plugin does to find the source cell by itself (reference for the C++ port).

    Per frame (hop 512): if the frame is loud enough (total level above gate_db) its band POWER joins a running mean whose
    memory is avg_seconds of loud frames (silence does not dilute it). The running mean gives a spectrum shape (3-band
    smoothing, band mean removed inside the mask), its rms distance in dB to every cell prototype gives soft weights
    softmax(-(d - d_min) / tau). Until min_frames loud frames were heard the weights stay uniform."""

    def __init__(self, protos, mask, tau_db=0.5, gate_db=-50.0, avg_seconds=1.0, min_frames=4):
        self.protos = np.asarray(protos, dtype=np.float64)        # (cells, mask.sum())
        self.mask = np.asarray(mask, dtype=bool)
        self.tau, self.gate, self.min_frames = float(tau_db), float(gate_db), float(min_frames)
        self.decay = math.exp(-(HOP / 48000.0) / float(avg_seconds))

    def distances(self, mean_power):
        clip = 10.0 * np.log10(mean_power + 1e-14)
        f = smooth3(clip)[self.mask]
        f = f - f.mean()
        return np.sqrt(np.mean((self.protos - f) ** 2, axis=1))

    def weights_from(self, d):
        z = np.exp(-(d - d.min()) / self.tau)
        return z / z.sum()

    def run(self, L):
        """L (F, 64) analysis band dB -> weights (F, cells) after each frame."""
        n = len(self.protos)
        acc, wsum = np.zeros(L.shape[1]), 0.0
        w = np.full(n, 1.0 / n)
        out = np.empty((len(L), n))
        for f in range(len(L)):
            p = 10.0 ** (L[f].astype(np.float64) / 10.0)
            if 10.0 * np.log10(p.sum() + 1e-14) > self.gate:
                acc = acc * self.decay + p
                wsum = wsum * self.decay + 1.0
                if wsum >= self.min_frames:
                    w = self.weights_from(self.distances(acc / wsum))
            out[f] = w
        return out


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
