"""Zarr v3 array-to-bytes codecs ``dp3core.dysco`` and ``dp3core.dysco-weights``.

Registered through the ``zarr.codecs`` entry point group (flat form, so the dotted
names survive). Unofficial DP3Core conventions, not endorsed by LOFAR, ASTRON or the
MSv4 schema group.
"""
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

import numpy as np
from zarr.abc.codec import ArrayBytesCodec
from zarr.core.common import parse_named_configuration

from ._config import DataConfig, WeightsConfig

if TYPE_CHECKING:
    from typing import Self

    from zarr.core.array_spec import ArraySpec
    from zarr.core.buffer import Buffer, NDBuffer

__all__ = ["DyscoCodec", "DyscoWeightsCodec"]


def _chunk_shapes(chunk_grid: Any) -> tuple[tuple[int, ...], ...] | None:
    """Per-dimension chunk edge lengths, or None if unknown."""
    shape = getattr(chunk_grid, "chunk_shape", None)
    if shape is not None:
        return tuple((int(s),) for s in shape)
    shapes = getattr(chunk_grid, "chunk_shapes", None)
    if shapes is not None:
        return tuple((int(s),) if isinstance(s, int) else tuple(int(x) for x in s) for s in shapes)
    return None


def _check_grid(config, shape, chunk_grid) -> None:
    """Design section 6: checks the array shape and every chunk extent."""
    name = "dp3core.dysco" if isinstance(config, DataConfig) else "dp3core.dysco-weights"
    if len(shape) != 4:
        raise ValueError(
            f"{name} needs a 4-dimensional (time, baseline, frequency, polarization) "
            f"array; got shape {tuple(shape)}")
    edges = _chunk_shapes(chunk_grid)
    if edges is None:
        return
    for b in sorted(set(edges[1])):
        for p in sorted(set(edges[3])):
            config.check_chunk_shape((edges[0][0], b, edges[2][0], p), tuple(shape))


class _DyscoBase(ArrayBytesCodec):
    is_fixed_size = True
    _name: str
    _dtype: np.dtype

    def to_dict(self) -> dict[str, Any]:
        return {"name": self._name, "configuration": self.config.to_json()}

    def validate(self, *, shape, dtype, chunk_grid) -> None:
        native = dtype.to_native_dtype()
        if np.dtype(native) != self._dtype:
            raise ValueError(f"{self._name} needs dtype {self._dtype}, got {native}")
        _check_grid(self.config, tuple(shape), chunk_grid)

    def compute_encoded_size(self, input_byte_length: int, chunk_spec: ArraySpec) -> int:
        return self.config.encoded_size(chunk_spec.shape)

    def _encode_sync(self, chunk_array: NDBuffer, chunk_spec: ArraySpec) -> Buffer | None:
        array = chunk_array.as_numpy_array()
        if array.dtype != self._dtype:
            array = array.astype(self._dtype)
        if array.shape != tuple(chunk_spec.shape):
            array = array.reshape(chunk_spec.shape)
        encoded = self.config.encode(array)
        return chunk_spec.prototype.buffer.from_bytes(encoded)

    def _decode_sync(self, chunk_bytes: Buffer, chunk_spec: ArraySpec) -> NDBuffer:
        raw = np.ascontiguousarray(chunk_bytes.as_numpy_array()).view(np.uint8).reshape(-1)
        decoded = self.config.decode(raw, tuple(chunk_spec.shape))
        return chunk_spec.prototype.nd_buffer.from_numpy_array(decoded)

    async def _encode_single(self, chunk_array: NDBuffer, chunk_spec: ArraySpec) -> Buffer | None:
        return await asyncio.to_thread(self._encode_sync, chunk_array, chunk_spec)

    async def _decode_single(self, chunk_bytes: Buffer, chunk_spec: ArraySpec) -> NDBuffer:
        return await asyncio.to_thread(self._decode_sync, chunk_bytes, chunk_spec)


@dataclass(frozen=True)
class DyscoCodec(_DyscoBase):
    """Dysco compression of complex64 visibilities (``dp3core.dysco``).

    Keyword arguments are the configuration fields of design section 4.1, e.g.
    ``DyscoCodec(bits=10, normalization="af", antenna1=a1, antenna2=a2, n_antennas=38)``.
    """

    config: DataConfig
    _name = "dp3core.dysco"
    _dtype = np.dtype(np.complex64)

    def __init__(self, *, config: DataConfig | None = None, **kwargs: Any) -> None:
        if config is None:
            if "antenna1" in kwargs and kwargs["antenna1"] is not None:
                kwargs["antenna1"] = tuple(int(a) for a in np.asarray(kwargs["antenna1"]).ravel())
            if "antenna2" in kwargs and kwargs["antenna2"] is not None:
                kwargs["antenna2"] = tuple(int(a) for a in np.asarray(kwargs["antenna2"]).ravel())
            config = DataConfig(**kwargs)
        elif kwargs:
            raise TypeError("pass either config or keyword fields, not both")
        object.__setattr__(self, "config", config)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> Self:
        _, configuration = parse_named_configuration(data, "dp3core.dysco", require_configuration=False)
        return cls(config=DataConfig.from_json(dict(configuration or {})))


@dataclass(frozen=True)
class DyscoWeightsCodec(_DyscoBase):
    """Linear quantization of float32 weights (``dp3core.dysco-weights``)."""

    config: WeightsConfig
    _name = "dp3core.dysco-weights"
    _dtype = np.dtype(np.float32)

    def __init__(self, *, config: WeightsConfig | None = None, **kwargs: Any) -> None:
        if config is None:
            config = WeightsConfig(**kwargs)
        elif kwargs:
            raise TypeError("pass either config or keyword fields, not both")
        object.__setattr__(self, "config", config)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> Self:
        _, configuration = parse_named_configuration(
            data, "dp3core.dysco-weights", require_configuration=False)
        return cls(config=WeightsConfig.from_json(dict(configuration or {})))
