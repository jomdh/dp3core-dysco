"""Codec configuration shared by the zarr v3 and numcodecs (zarr v2) codecs.

Field names and rules follow design section 4 / spec/SPEC.md.
"""
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import numpy as np

from . import _core

AXES = ("time", "baseline", "frequency", "polarization")
BITS = (2, 3, 4, 6, 8, 10, 12, 16)
DISTRIBUTIONS = ("gaussian", "truncated_gaussian", "uniform")
NORMALIZATIONS = ("af", "rf", "row")
PRNGS = ("splitmix64-v1",)
DICTIONARIES = ("computed", "embedded")
MAX_UINT64 = 2**64 - 1


def _parse_int(value: Any, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, np.integer, str)):
        raise TypeError(f"{name} must be an integer, got {value!r}")
    try:
        return int(value)
    except ValueError as e:
        raise ValueError(f"{name} must be an integer, got {value!r}") from e


def _parse_axes(axes: Any) -> tuple[str, ...]:
    axes = tuple(axes)
    if sorted(axes) != sorted(AXES):
        raise ValueError(f"axes must be a permutation of {list(AXES)}, got {list(axes)}")
    if axes != AXES:
        # Format 1 of this implementation stores the canonical order only.
        raise NotImplementedError(
            f"axes {list(axes)}: only {list(AXES)} is supported; add a 'transpose' "
            "codec in front of this codec instead"
        )
    return axes


def _parse_format_version(value: Any) -> int:
    v = _parse_int(value, "format_version")
    if v < 1 or v > _core.FORMAT_VERSION:
        raise ValueError(
            f"format_version {v} is not supported (this library reads 1..{_core.FORMAT_VERSION})"
        )
    return v


@dataclass(frozen=True)
class DataConfig:
    """Configuration of ``dp3core.dysco`` (complex64 visibilities)."""

    bits: int = 10
    distribution: str = "truncated_gaussian"
    truncation: float | None = 2.5
    normalization: str = "af"
    axes: tuple[str, ...] = AXES
    n_antennas: int | None = None
    antenna1: tuple[int, ...] | None = None
    antenna2: tuple[int, ...] | None = None
    prng: str = "splitmix64-v1"
    seed: int = 0
    dictionary: str = "computed"
    embed_antenna_map: bool = False
    format_version: int = 1

    def __post_init__(self) -> None:
        s = object.__setattr__
        s(self, "format_version", _parse_format_version(self.format_version))
        bits = _parse_int(self.bits, "bits")
        if bits not in BITS:
            raise ValueError(f"bits must be one of {list(BITS)}, got {bits}")
        s(self, "bits", bits)
        if self.distribution not in DISTRIBUTIONS:
            raise ValueError(f"distribution must be one of {list(DISTRIBUTIONS)}, got {self.distribution!r}")
        if self.distribution == "truncated_gaussian":
            if self.truncation is None:
                raise ValueError("truncation is required for distribution 'truncated_gaussian'")
            t = float(self.truncation)
            if not (0.0 < t <= 8.0):
                raise ValueError(f"truncation must be in (0, 8], got {t}")
            s(self, "truncation", t)
        else:
            s(self, "truncation", None)
        if self.normalization not in NORMALIZATIONS:
            raise ValueError(f"normalization must be one of {list(NORMALIZATIONS)}, got {self.normalization!r}")
        s(self, "axes", _parse_axes(self.axes))
        if self.prng not in PRNGS:
            raise ValueError(f"prng must be one of {list(PRNGS)}, got {self.prng!r}")
        seed = _parse_int(self.seed, "seed")
        if not 0 <= seed <= MAX_UINT64:
            raise ValueError(f"seed must be in [0, 2^64), got {seed}")
        s(self, "seed", seed)
        if self.dictionary not in DICTIONARIES:
            raise ValueError(f"dictionary must be one of {list(DICTIONARIES)}, got {self.dictionary!r}")
        if not isinstance(self.embed_antenna_map, (bool, np.bool_)):
            raise TypeError("embed_antenna_map must be a boolean")
        s(self, "embed_antenna_map", bool(self.embed_antenna_map))
        if self.normalization == "af":
            if self.n_antennas is None or self.antenna1 is None or self.antenna2 is None:
                raise ValueError("normalization 'af' requires n_antennas, antenna1 and antenna2")
            n = _parse_int(self.n_antennas, "n_antennas")
            a1 = tuple(_parse_int(a, "antenna1") for a in self.antenna1)
            a2 = tuple(_parse_int(a, "antenna2") for a in self.antenna2)
            if n < 1:
                raise ValueError(f"n_antennas must be >= 1, got {n}")
            if len(a1) != len(a2):
                raise ValueError(f"antenna1 has {len(a1)} entries, antenna2 {len(a2)}")
            bad = [a for a in a1 + a2 if not 0 <= a < n]
            if bad:
                raise ValueError(f"antenna index {bad[0]} is not in [0, n_antennas={n})")
            s(self, "n_antennas", n)
            s(self, "antenna1", a1)
            s(self, "antenna2", a2)
        else:
            if self.embed_antenna_map:
                raise ValueError("embed_antenna_map is only allowed with normalization 'af'")
            if self.n_antennas is not None or self.antenna1 is not None or self.antenna2 is not None:
                raise ValueError("n_antennas/antenna1/antenna2 are only allowed with normalization 'af'")

    # -- serialization -----------------------------------------------------
    def to_json(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "format_version": self.format_version,
            "bits": self.bits,
            "distribution": self.distribution,
        }
        if self.truncation is not None:
            d["truncation"] = self.truncation
        d["normalization"] = self.normalization
        d["axes"] = list(self.axes)
        if self.normalization == "af":
            d["n_antennas"] = self.n_antennas
            d["antenna1"] = list(self.antenna1)
            d["antenna2"] = list(self.antenna2)
        d["prng"] = self.prng
        d["seed"] = self.seed
        d["dictionary"] = self.dictionary
        d["embed_antenna_map"] = self.embed_antenna_map
        return d

    @classmethod
    def from_json(cls, d: dict[str, Any]) -> DataConfig:
        known = {f for f in cls.__dataclass_fields__}
        unknown = set(d) - known
        if unknown:
            raise ValueError(f"unknown dp3core.dysco configuration fields: {sorted(unknown)}")
        return cls(**d)

    # -- core calls ----------------------------------------------------------
    def _kwargs(self) -> dict[str, Any]:
        kw: dict[str, Any] = dict(
            bits=self.bits,
            distribution=self.distribution,
            truncation=self.truncation if self.truncation is not None else 0.0,
            normalization=self.normalization,
            embed_dictionary=self.dictionary == "embedded",
            embed_antenna_map=self.embed_antenna_map,
        )
        if self.normalization == "af":
            kw.update(antenna1=list(self.antenna1), antenna2=list(self.antenna2),
                      n_antennas=self.n_antennas)
        return kw

    def _map_kwargs(self) -> dict[str, Any]:
        if self.normalization != "af":
            return {}
        return dict(antenna1=list(self.antenna1), antenna2=list(self.antenna2),
                    n_antennas=self.n_antennas)

    def check_chunk_shape(self, chunk_shape: tuple[int, ...], array_shape: tuple[int, ...] | None = None) -> None:
        """Design section 6 rules for a chunk (and, if known, the array)."""
        if len(chunk_shape) != 4:
            raise ValueError(
                f"dp3core.dysco needs 4-dimensional (time, baseline, frequency, polarization) "
                f"chunks, got shape {tuple(chunk_shape)}")
        if self.normalization == "af":
            if array_shape is not None and array_shape[1] != chunk_shape[1]:
                raise ValueError(
                    "AF needs the whole baseline dimension in one chunk "
                    f"(array {array_shape[1]}, chunk {chunk_shape[1]})")
            if chunk_shape[1] != len(self.antenna1):
                raise ValueError(
                    f"AF: the chunk has {chunk_shape[1]} baselines but antenna1/antenna2 have "
                    f"{len(self.antenna1)} entries")
        if self.normalization == "row" and array_shape is not None and array_shape[3] != chunk_shape[3]:
            raise ValueError("Row normalization needs the whole polarization dimension in one chunk")

    def encoded_size(self, chunk_shape) -> int:
        return _core.encoded_size_data(list(chunk_shape), **self._kwargs())

    def encode(self, array: np.ndarray, threads: int = 1) -> bytes:
        array = np.ascontiguousarray(array, dtype=np.complex64)
        self.check_chunk_shape(array.shape)
        return _core.encode_data(array, seed=self.seed, threads=threads, **self._kwargs())

    def decode(self, encoded, chunk_shape) -> np.ndarray:
        return _core.decode_data(encoded, list(chunk_shape), **self._map_kwargs())


@dataclass(frozen=True)
class WeightsConfig:
    """Configuration of ``dp3core.dysco-weights`` (float32 weights)."""

    bits: int = 12
    axes: tuple[str, ...] = AXES
    format_version: int = 1

    def __post_init__(self) -> None:
        s = object.__setattr__
        s(self, "format_version", _parse_format_version(self.format_version))
        bits = _parse_int(self.bits, "bits")
        if bits not in BITS:
            raise ValueError(f"bits must be one of {list(BITS)}, got {bits}")
        s(self, "bits", bits)
        s(self, "axes", _parse_axes(self.axes))

    def to_json(self) -> dict[str, Any]:
        return {"format_version": self.format_version, "bits": self.bits, "axes": list(self.axes)}

    @classmethod
    def from_json(cls, d: dict[str, Any]) -> WeightsConfig:
        unknown = set(d) - set(cls.__dataclass_fields__)
        if unknown:
            raise ValueError(f"unknown dp3core.dysco-weights configuration fields: {sorted(unknown)}")
        return cls(**d)

    def check_chunk_shape(self, chunk_shape, array_shape=None) -> None:
        if len(chunk_shape) != 4:
            raise ValueError(
                f"dp3core.dysco-weights needs 4-dimensional chunks, got shape {tuple(chunk_shape)}")
        if array_shape is not None and array_shape[3] != chunk_shape[3]:
            raise ValueError("weights need the whole polarization dimension in one chunk")

    def encoded_size(self, chunk_shape) -> int:
        return _core.encoded_size_weights(list(chunk_shape), bits=self.bits)

    def encode(self, array: np.ndarray, threads: int = 1) -> bytes:
        array = np.ascontiguousarray(array, dtype=np.float32)
        self.check_chunk_shape(array.shape)
        return _core.encode_weights(array, bits=self.bits, threads=threads)

    def decode(self, encoded, chunk_shape) -> np.ndarray:
        return _core.decode_weights(encoded, list(chunk_shape))
