# SPDX-License-Identifier: GPL-3.0-only
"""zarr-python <-> TensorStore interoperability (design 7.3 spike acceptance).

Needs the dp3core_dysco_ts_copy tool from the TensorStore build; set
DP3CORE_DYSCO_TS_COPY to its path (scripts/check-tensorstore.sh does).

zarr-python writes an array; the tool decodes it through TensorStore (must equal
zarr-python's decode exactly) and encodes the same *original* input into a new
array with the same metadata (chunk files must be byte-identical). The original
input is used because the dither nonce hashes the encoder input (design 5.2).
"""
import os
import subprocess
from pathlib import Path

import numpy as np
import pytest
import zarr
from fixtures import GOLDEN_SEED, make_fixture
from zarr.codecs import Crc32cCodec, ZstdCodec

from dp3core_dysco.zarr import DyscoCodec, DyscoWeightsCodec

TOOL = os.environ.get("DP3CORE_DYSCO_TS_COPY")
pytestmark = pytest.mark.skipif(not TOOL, reason="DP3CORE_DYSCO_TS_COPY not set")


def chunk_files(root: Path) -> dict[str, bytes]:
    return {str(p.relative_to(root)): p.read_bytes()
            for p in sorted(root.rglob("*")) if p.is_file() and p.name != "zarr.json"}


def tensorstore_roundtrip(src: Path, dst: Path, original: np.ndarray) -> np.ndarray:
    """Returns TensorStore's decode of src; writes original to dst via TensorStore."""
    raw_in, raw_out = dst.with_suffix(".in"), dst.with_suffix(".out")
    np.ascontiguousarray(original).tofile(raw_in)
    r = subprocess.run([TOOL, str(src), str(dst), str(raw_in), str(raw_out)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return np.fromfile(raw_out, dtype=original.dtype).reshape(original.shape)


@pytest.mark.parametrize("norm", ["row", "rf", "af"])
def test_data_interop(tmp_path, norm):
    f = make_fixture(5, 6, 8, 4, 2024)
    kw = dict(antenna1=f.antenna1, antenna2=f.antenna2, n_antennas=f.n_antennas) if norm == "af" else {}
    src = tmp_path / "py.zarr"
    arr = zarr.create_array(src, shape=f.shape, chunks=(2, f.shape[1], 4, 4), dtype="complex64",
                            serializer=DyscoCodec(normalization=norm, seed=GOLDEN_SEED, **kw),
                            compressors=[Crc32cCodec(), ZstdCodec(level=3)], fill_value=0)
    arr[...] = f.data
    dst = tmp_path / "ts.zarr"
    ts_decoded = tensorstore_roundtrip(src, dst, f.data)
    np.testing.assert_array_equal(ts_decoded, arr[...])
    a, b = chunk_files(src), chunk_files(dst)
    assert a.keys() == b.keys() and len(a) == 6
    for k in a:
        assert a[k] == b[k], k
    np.testing.assert_array_equal(zarr.open_array(dst, mode="r")[...], arr[...])


def test_weights_interop(tmp_path):
    f = make_fixture(3, 5, 4, 4, 12)
    src = tmp_path / "w.zarr"
    arr = zarr.create_array(src, shape=f.shape, chunks=(1, 15, 4, 4), dtype="float32",
                            serializer=DyscoWeightsCodec(bits=10), compressors=None, fill_value=0)
    arr[...] = f.weights
    dst = tmp_path / "wts.zarr"
    ts_decoded = tensorstore_roundtrip(src, dst, f.weights)
    np.testing.assert_array_equal(ts_decoded, arr[...])
    assert chunk_files(src) == chunk_files(dst)
