"""The C++ test fixtures (core/tests/test_codec.cc), regenerated in Python.

The generator uses only integer arithmetic and IEEE-754 basic double operations, so
the arrays are bit-identical to the C++ ones, and the golden hashes below are the
same constants as in test_codec.cc's GoldenFixtures.
"""
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

_M = (1 << 64) - 1


def mix64(z: int) -> int:
    z = (z + 0x9E3779B97F4A7C15) & _M
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & _M
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & _M
    return z ^ (z >> 31)


@dataclass
class Fixture:
    data: np.ndarray  # complex64 (time, baseline, frequency, polarization)
    weights: np.ndarray  # float32, same shape
    antenna1: list[int]
    antenna2: list[int]
    n_antennas: int

    @property
    def shape(self) -> tuple[int, ...]:
        return self.data.shape


def make_fixture(n_time: int, n_ant: int, n_chan: int, n_pol: int, seed: int) -> Fixture:
    a1, a2 = [], []
    for i in range(n_ant):
        for j in range(i, n_ant):
            a1.append(i)
            a2.append(j)
    n_bl = len(a1)
    state = seed

    def u() -> float:
        nonlocal state
        state = (state + 1) & _M
        return (mix64(state) >> 11) / float(1 << 53)

    gain = [0.5 + 2.0 * u() for _ in range(n_ant)]
    chan = [0.5 + u() for _ in range(n_chan * n_pol)]
    data = np.empty((n_time, n_bl, n_chan, n_pol), np.complex64)
    weights = np.empty((n_time, n_bl, n_chan, n_pol), np.float32)
    for t in range(n_time):
        for b in range(n_bl):
            for ch in range(n_chan):
                for p in range(n_pol):
                    is_auto = a1[b] == a2[b]
                    scale = (gain[a1[b]] * gain[a2[b]] * chan[ch * n_pol + p]
                             * (20.0 if is_auto else 1.0) * (1.0 + 0.1 * float(t)))
                    re = np.float32(scale * (u() - 0.5))
                    im = np.float32(scale * (u() - 0.5))
                    data[t, b, ch, p] = np.complex64(complex(re, im))
                    weights[t, b, ch, p] = np.float32(math.floor(u() * 1000.0)) * np.float32(0.25)
    return Fixture(data, weights, a1, a2, n_ant)


# test_codec.cc GoldenFixtures: MakeFixture(3, 5, 4, 4, 12), seed 0x5EED, default
# bits/distribution/truncation; (XXH3 of the encoded chunk, XXH3 of the decoded values).
GOLDEN_FIXTURE_ARGS = (3, 5, 4, 4, 12)
GOLDEN_SEED = 0x5EED
GOLDEN = {
    "row": (0x1BA8547955090C7E, 0x0C10D3C2E1B56D65),
    "rf": (0x2DE51A6AAE790C1A, 0x3948D4BE8DB707BD),
    "af": (0xC9817C878BF2AF2A, 0x24895812C0A32DF7),
    "weights": (0x1D3D8EFD2346BF58, 0x1781051CAC6F0BBE),
}
