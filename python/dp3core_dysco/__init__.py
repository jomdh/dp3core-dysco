"""Dysco lossy compression of radio-interferometric visibilities as Zarr codecs.

Convention notice: the codec names ``dp3core.dysco`` and ``dp3core.dysco-weights``,
their configuration fields and the chunk layout are unofficial DP3Core conventions,
not endorsed by LOFAR, ASTRON or the MSv4 schema group.
"""
# SPDX-License-Identifier: GPL-3.0-only
from ._core import (
    FORMAT_VERSION,
    FormatError,
    __version__,
    decode_data,
    decode_table,
    decode_weights,
    encode_data,
    encode_weights,
    read_header,
)
from ._config import DataConfig, WeightsConfig

__all__ = [
    "FORMAT_VERSION",
    "DataConfig",
    "DyscoCodec",
    "DyscoWeightsCodec",
    "FormatError",
    "WeightsConfig",
    "__version__",
    "decode_data",
    "decode_table",
    "decode_weights",
    "encode_data",
    "encode_weights",
    "read_header",
]


def __getattr__(name):
    # Imported lazily so that ``import dp3core_dysco`` does not need zarr.
    if name in ("DyscoCodec", "DyscoWeightsCodec"):
        from . import zarr as _zarr

        return getattr(_zarr, name)
    raise AttributeError(name)
