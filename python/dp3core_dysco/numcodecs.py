"""numcodecs (Zarr v2) codecs ``dp3core.dysco`` and ``dp3core.dysco-weights``.

Used as a v2 ``compressor`` with no filters. Zarr hands v2 compressors a flat byte
buffer, so the chunk shape comes from ``chunk_shape`` and is checked against the
chunk header (design section 4.3). Unofficial DP3Core conventions.
"""
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

from typing import Any

import numpy as np
from numcodecs.abc import Codec
from numcodecs.compat import ensure_contiguous_ndarray, ndarray_copy

from ._config import DataConfig, WeightsConfig

__all__ = ["Dysco", "DyscoWeights"]


class _Base(Codec):
    _config_cls: type
    _dtype: np.dtype

    def __init__(self, chunk_shape, **kwargs: Any) -> None:
        chunk_shape = tuple(int(s) for s in chunk_shape)
        if len(chunk_shape) != 4:
            raise ValueError(f"chunk_shape must have 4 entries, got {chunk_shape}")
        self.chunk_shape = chunk_shape
        for key in ("antenna1", "antenna2"):
            if kwargs.get(key) is not None:
                kwargs[key] = tuple(int(a) for a in np.asarray(kwargs[key]).ravel())
        if "axes" in kwargs:
            kwargs["axes"] = tuple(kwargs["axes"])
        self._config = self._config_cls(**kwargs)
        self._config.check_chunk_shape(chunk_shape)

    def get_config(self) -> dict[str, Any]:
        return {"id": self.codec_id, **self._config.to_json(), "chunk_shape": list(self.chunk_shape)}

    @classmethod
    def from_config(cls, config: dict[str, Any]):
        config = dict(config)
        config.pop("id", None)
        return cls(**config)

    def __repr__(self) -> str:
        return f"{type(self).__name__}({self.get_config()!r})"

    def encode(self, buf):
        arr = ensure_contiguous_ndarray(buf).view(self._dtype)
        n = int(np.prod(self.chunk_shape))
        if arr.size != n:
            raise ValueError(f"{self.codec_id}: got {arr.size} values, chunk_shape needs {n}")
        return self._config.encode(arr.reshape(self.chunk_shape))

    def decode(self, buf, out=None):
        raw = ensure_contiguous_ndarray(buf).view(np.uint8).reshape(-1)
        decoded = self._config.decode(raw, self.chunk_shape)
        return ndarray_copy(decoded, out)


class Dysco(_Base):
    """``dp3core.dysco`` for complex64 visibilities (Zarr v2 / numcodecs)."""

    codec_id = "dp3core.dysco"
    _config_cls = DataConfig
    _dtype = np.dtype(np.complex64)


class DyscoWeights(_Base):
    """``dp3core.dysco-weights`` for float32 weights (Zarr v2 / numcodecs)."""

    codec_id = "dp3core.dysco-weights"
    _config_cls = WeightsConfig
    _dtype = np.dtype(np.float32)
