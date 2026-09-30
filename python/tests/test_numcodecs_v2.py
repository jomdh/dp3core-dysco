# SPDX-License-Identifier: GPL-3.0-only
import json

import numcodecs
import numpy as np
import pytest
import zarr
from fixtures import GOLDEN, GOLDEN_SEED

from dp3core_dysco import _core
from dp3core_dysco.numcodecs import Dysco, DyscoWeights


def af(f):
    return dict(antenna1=f.antenna1, antenna2=f.antenna2, n_antennas=f.n_antennas)


def test_registry_and_config_round_trip(golden_fixture):
    f = golden_fixture
    c = Dysco(chunk_shape=f.shape, normalization="af", seed=GOLDEN_SEED, **af(f))
    cfg = c.get_config()
    assert cfg["id"] == "dp3core.dysco" and cfg["chunk_shape"] == list(f.shape)
    c2 = numcodecs.get_codec(json.loads(json.dumps(cfg)))
    assert isinstance(c2, Dysco) and c2.get_config() == cfg
    w = numcodecs.get_codec({"id": "dp3core.dysco-weights", "chunk_shape": list(f.shape)})
    assert isinstance(w, DyscoWeights)


@pytest.mark.parametrize("norm", ["row", "rf", "af"])
def test_golden_codec(golden_fixture, norm):
    f = golden_fixture
    c = Dysco(chunk_shape=f.shape, normalization=norm, seed=GOLDEN_SEED,
              **(af(f) if norm == "af" else {}))
    enc = c.encode(f.data.tobytes())
    assert _core.xxh3(enc) == GOLDEN[norm][0]
    out = np.empty_like(f.data)
    c.decode(enc, out=out)
    assert _core.xxh3(out) == GOLDEN[norm][1]


@pytest.mark.parametrize("norm", ["row", "af"])
def test_zarr_v2_array(tmp_path, golden_fixture, norm):
    f = golden_fixture
    comp = Dysco(chunk_shape=f.shape, normalization=norm, seed=GOLDEN_SEED,
                 **(af(f) if norm == "af" else {}))
    arr = zarr.create_array(tmp_path / "v2.zarr", shape=f.shape, chunks=f.shape,
                            dtype="complex64", zarr_format=2, compressors=comp, filters=None,
                            fill_value=0)
    arr[...] = f.data
    stored = (tmp_path / "v2.zarr" / "0.0.0.0").read_bytes()
    assert _core.xxh3(stored) == GOLDEN[norm][0]
    meta = json.loads((tmp_path / "v2.zarr" / ".zarray").read_text())
    assert meta["compressor"]["id"] == "dp3core.dysco"
    back = zarr.open_array(tmp_path / "v2.zarr", mode="r")[...]
    assert _core.xxh3(back) == GOLDEN[norm][1]


def test_zarr_v2_weights(tmp_path, golden_fixture):
    f = golden_fixture
    arr = zarr.create_array(tmp_path / "w.zarr", shape=f.shape, chunks=f.shape, dtype="float32",
                            zarr_format=2, compressors=DyscoWeights(chunk_shape=f.shape),
                            filters=None, fill_value=0)
    arr[...] = f.weights
    assert _core.xxh3((tmp_path / "w.zarr" / "0.0.0.0").read_bytes()) == GOLDEN["weights"][0]
    assert _core.xxh3(zarr.open_array(tmp_path / "w.zarr", mode="r")[...]) == GOLDEN["weights"][1]


def test_chunk_shape_mismatch(golden_fixture):
    f = golden_fixture
    c = Dysco(chunk_shape=(1,) + f.shape[1:], normalization="row")
    with pytest.raises(ValueError, match="chunk_shape needs"):
        c.encode(f.data.tobytes())
    enc = Dysco(chunk_shape=f.shape, normalization="row").encode(f.data.tobytes())
    with pytest.raises(ValueError):
        c.decode(enc)
    with pytest.raises(ValueError, match="4 entries"):
        Dysco(chunk_shape=(3, 4), normalization="row")
