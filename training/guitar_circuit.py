"""Frequency-domain model of guitar electronics.

Models what happens between the strings and the cable: pickup (inductance + DC
resistance), volume pot, tone pot + capacitor, pickup selector, cable capacitance and
the input impedance of whatever is plugged in.

This is the "physical front end" of 4real-audio. It captures the part of a pickup's
character that is a fixed linear filter (resonant peak, how the volume and tone knobs
load the pickup, how pickups combine in parallel). It does NOT capture the
pitch-dependent effects of pickup position and width, or any non-linear behaviour.
Those are what the neural network is meant to learn later.

All component values below are typical published figures, NOT measurements of any
specific guitar. Measure your own pickups (DC resistance with a multimeter, inductance
with an LCR meter) and edit the presets.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import ClassVar

import numpy as np

GND = "gnd"
REF_INDUCTANCE_H = 2.4  # reference for the rough sensitivity estimate


def taper(x: float, exponent: float = 2.5) -> float:
    """Audio-taper-like mapping from knob position 0..1 to resistance fraction."""
    return float(np.clip(x, 0.0, 1.0)) ** exponent


@dataclass(frozen=True)
class Pickup:
    name: str
    inductance_h: float
    dcr_ohm: float
    # Open-circuit output relative to a 2.4 H pickup. If None it is estimated from the
    # inductance (output voltage grows roughly with the square root of inductance).
    sensitivity_db: float | None = None

    def gain(self) -> float:
        db = self.sensitivity_db
        if db is None:
            db = 10.0 * np.log10(self.inductance_h / REF_INDUCTANCE_H)
        return float(10.0 ** (db / 20.0))


@dataclass(frozen=True)
class Rig:
    """What the guitar is plugged into."""

    cable_pf: float = 500.0
    amp_input_ohm: float = 1.0e6


class Net:
    """Tiny nodal-analysis solver (complex admittances, one solve per frequency)."""

    def __init__(self, freqs_hz):
        self.freqs = np.asarray(freqs_hz, dtype=float)
        self.s = 2j * np.pi * self.freqs
        self._idx: dict[str, int] = {}
        self._elems: list[tuple[int | None, int | None, np.ndarray]] = []
        self._sources: list[tuple[int, np.ndarray]] = []

    def _ix(self, name: str) -> int | None:
        if name == GND:
            return None
        if name not in self._idx:
            self._idx[name] = len(self._idx)
        return self._idx[name]

    def admittance(self, a: str, b: str, y: np.ndarray) -> None:
        self._elems.append((self._ix(a), self._ix(b), y))

    def resistor(self, a: str, b: str, ohm: float) -> None:
        ohm = max(float(ohm), 1.0)  # 1 ohm floor keeps the matrix well conditioned
        self.admittance(a, b, np.full(self.s.shape, 1.0 / ohm, dtype=complex))

    def capacitor(self, a: str, b: str, farad: float) -> None:
        self.admittance(a, b, self.s * farad)

    def series_rc(self, a: str, b: str, ohm: float, farad: float) -> None:
        ohm = max(float(ohm), 1.0)
        self.admittance(a, b, self.s * farad / (1.0 + self.s * farad * ohm))

    def pickup(self, node: str, p: Pickup) -> None:
        """Pickup as a voltage source behind R + sL, written as a Norton equivalent."""
        z = p.dcr_ohm + self.s * p.inductance_h
        self.admittance(node, GND, 1.0 / z)
        self._sources.append((self._ix(node), p.gain() / z))

    def solve(self, out: str) -> np.ndarray:
        n = len(self._idx)
        f = self.s.size
        y = np.zeros((f, n, n), dtype=complex)
        i = np.zeros((f, n), dtype=complex)
        for ia, ib, adm in self._elems:
            if ia is not None:
                y[:, ia, ia] += adm
            if ib is not None:
                y[:, ib, ib] += adm
            if ia is not None and ib is not None:
                y[:, ia, ib] -= adm
                y[:, ib, ia] -= adm
        for node, cur in self._sources:
            i[:, node] += cur
        v = np.linalg.solve(y, i[..., None])[..., 0]
        return v[:, self._idx[out]]


def _add_volume(net: Net, hot: str, wiper: str, pot_ohm: float, position: float) -> None:
    a = taper(position / 10.0)
    net.resistor(hot, wiper, (1.0 - a) * pot_ohm)
    net.resistor(wiper, GND, a * pot_ohm)


def _add_tone(net: Net, node: str, pot_ohm: float, cap_f: float, position: float) -> None:
    # Tone at 10 = pot at full resistance (cap effectively out); at 0 = cap fully across.
    net.series_rc(node, GND, taper(position / 10.0) * pot_ohm, cap_f)


def _add_output(net: Net, rig: Rig) -> None:
    net.capacitor("out", GND, rig.cable_pf * 1e-12)
    net.resistor("out", GND, rig.amp_input_ohm)


@dataclass
class StratCircuit:
    """5-way selector, one master volume, two tone knobs (neck and middle).

    Wiring varies between guitars; this uses the common layout: tone 1 acts when the
    neck pickup is selected, tone 2 when the middle pickup is selected, bridge has none.
    """

    neck: Pickup
    mid: Pickup
    bridge: Pickup
    pot_ohm: float = 250e3
    tone_cap_f: float = 22e-9
    name: str = "stratocaster"
    POSITIONS: ClassVar[dict[int, tuple[str, ...]]] = {
        1: ("bridge",),
        2: ("bridge", "mid"),
        3: ("mid",),
        4: ("mid", "neck"),
        5: ("neck",),
    }

    def response(self, freqs, rig: Rig = Rig(), position: int = 1, volume: float = 10.0,
                 tone_neck: float = 10.0, tone_mid: float = 10.0) -> np.ndarray:
        net = Net(freqs)
        pickups = {"neck": self.neck, "mid": self.mid, "bridge": self.bridge}
        selected = self.POSITIONS[int(position)]
        for key in selected:
            net.pickup("hot", pickups[key])
        _add_volume(net, "hot", "out", self.pot_ohm, volume)
        if "neck" in selected:
            _add_tone(net, "hot", self.pot_ohm, self.tone_cap_f, tone_neck)
        if "mid" in selected:
            _add_tone(net, "hot", self.pot_ohm, self.tone_cap_f, tone_mid)
        _add_output(net, rig)
        return net.solve("out")


@dataclass
class LesPaulCircuit:
    """3-way selector, independent volume and tone per pickup (2 vol, 2 tone).

    tone_tap="hot": tone knob sits on the pickup side of its volume pot (volume does not
    affect tone). tone_tap="wiper": tone sits after the volume pot. Wiring varies.
    """

    neck: Pickup
    bridge: Pickup
    pot_ohm: float = 500e3
    tone_cap_f: float = 22e-9
    tone_tap: str = "hot"
    name: str = "lespaul"
    SELECTORS: ClassVar[dict[str, tuple[str, ...]]] = {
        "neck": ("neck",),
        "both": ("neck", "bridge"),
        "bridge": ("bridge",),
    }

    def response(self, freqs, rig: Rig = Rig(), selector: str = "bridge",
                 vol_neck: float = 10.0, vol_bridge: float = 10.0,
                 tone_neck: float = 10.0, tone_bridge: float = 10.0) -> np.ndarray:
        net = Net(freqs)
        pickups = {"neck": self.neck, "bridge": self.bridge}
        vols = {"neck": vol_neck, "bridge": vol_bridge}
        tones = {"neck": tone_neck, "bridge": tone_bridge}
        for key in self.SELECTORS[selector]:
            hot, wiper = f"hot_{key}", f"wiper_{key}"
            net.pickup(hot, pickups[key])
            _add_volume(net, hot, wiper, self.pot_ohm, vols[key])
            tone_node = hot if self.tone_tap == "hot" else wiper
            _add_tone(net, tone_node, self.pot_ohm, self.tone_cap_f, tones[key])
            net.resistor(wiper, "out", 1.0)  # closed selector contact
        _add_output(net, rig)
        return net.solve("out")


# --- Presets (typical values, NOT measurements; edit with your own) -------------------

def strat_sss() -> StratCircuit:
    return StratCircuit(
        neck=Pickup("Strat neck single coil", 2.2, 5.8e3),
        mid=Pickup("Strat middle single coil", 2.3, 5.9e3),
        bridge=Pickup("Strat bridge single coil", 2.6, 6.6e3),
    )


def strat_hss() -> StratCircuit:
    return StratCircuit(
        neck=Pickup("Strat neck single coil", 2.2, 5.8e3),
        mid=Pickup("Strat middle single coil", 2.3, 5.9e3),
        bridge=Pickup("HSS bridge humbucker (MEASURE YOURS)", 4.0, 9.0e3),
        name="stratocaster-hss",
    )


def les_paul() -> LesPaulCircuit:
    return LesPaulCircuit(
        neck=Pickup("PAF-style neck humbucker", 4.5, 7.5e3),
        bridge=Pickup("PAF-style bridge humbucker", 5.5, 8.5e3),
    )


GUITARS = {"strat-sss": strat_sss, "strat-hss": strat_hss, "lp": les_paul}


def parse_spec(spec: str):
    """'lp:selector=bridge,vol_bridge=7' -> (guitar_instance, controls_dict)."""
    name, _, rest = spec.partition(":")
    if name not in GUITARS:
        raise ValueError(f"unknown guitar '{name}', choose from {sorted(GUITARS)}")
    controls: dict[str, float | str] = {}
    for item in filter(None, rest.split(",")):
        key, _, value = item.partition("=")
        try:
            controls[key.strip()] = float(value)
        except ValueError:
            controls[key.strip()] = value.strip()
    return GUITARS[name](), controls
