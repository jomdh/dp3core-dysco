# SPDX-License-Identifier: GPL-3.0-only
import json
import warnings

import numpy as np
import pytest
import zarr
from fixtures import GOLDEN, GOLDEN_SEED, make_fixture
from zarr.codecs import Crc32cCodec, ShardingCodec, ZstdCodec
from zarr.registry import get_codec_class

import dp3core_dysco as d
from dp3core_dysco import _core
from dp3core_dysco.zarr import DyscoCodec, DyscoWeightsCodec


def af(f):
    return dict(antenna1=f.antenna1, antenna2=f.antenna2, n_antennas=f.n_antennas)


def codec_for(norm, f, **kw):
    return DyscoCodec(normalization=norm, **(af(f) if norm == "af" else {}), **kw)


def chunk_bytes(root, name, key="c/0/0/0/0"):
    return (root / name / key).read_bytes()


def test_entry_points():
    assert get_codec_class("dp3core.dysco") is DyscoCodec
    assert get_codec_class("dp3core.dysco-weights") is DyscoWeightsCodec


@pytest.mark.parametrize("norm", ["row", "rf", "af"])
def test_golden_through_zarr(tmp_path, golden_fixture, norm):
    f = golden_fixture
    arr = zarr.create_array(tmp_path / "a.zarr", shape=f.shape, chunks=f.shape, dtype="complex64",
                            serializer=codec_for(norm, f, seed=GOLDEN_SEED), compressors=None,
                            fill_value=0)
    arr[...] = f.data
    stored = (tmp_path / "a.zarr" / "c" / "0" / "0" / "0" / "0").read_bytes()
    assert _core.xxh3(stored) == GOLDEN[norm][0]
    # Re-open from the store: the codec is rebuilt from zarr.json by entry point.
    back = zarr.open_array(tmp_path / "a.zarr", mode="r")[...]
    assert back.dtype == np.complex64
    assert _core.xxh3(back) == GOLDEN[norm][1]


def test_golden_weights_through_zarr(tmp_path, golden_fixture):
    f = golden_fixture
    arr = zarr.create_array(tmp_path / "w.zarr", shape=f.shape, chunks=f.shape, dtype="float32",
                            serializer=DyscoWeightsCodec(), compressors=None, fill_value=0)
    arr[...] = f.weights
    stored = (tmp_path / "w.zarr" / "c" / "0" / "0" / "0" / "0").read_bytes()
    assert _core.xxh3(stored) == GOLDEN["weights"][0]
    back = zarr.open_array(tmp_path / "w.zarr", mode="r")[...]
    assert _core.xxh3(back) == GOLDEN["weights"][1]


def test_metadata(tmp_path, golden_fixture):
    f = golden_fixture
    zarr.create_array(tmp_path / "a.zarr", shape=f.shape, chunks=f.shape, dtype="complex64",
                      serializer=codec_for("af", f, bits=8, seed=2**63 + 5),
                      compressors=[Crc32cCodec()])
    meta = json.loads((tmp_path / "a.zarr" / "zarr.json").read_text())
    codecs = meta["codecs"]
    assert codecs[0]["name"] == "dp3core.dysco" and codecs[1]["name"] == "crc32c"
    cfg = codecs[0]["configuration"]
    assert cfg == {
        "format_version": 1, "bits": 8, "distribution": "truncated_gaussian", "truncation": 2.5,
        "normalization": "af", "axes": ["time", "baseline", "frequency", "polarization"],
        "n_antennas": 5, "antenna1": f.antenna1, "antenna2": f.antenna2,
        "prng": "splitmix64-v1", "seed": 2**63 + 5, "dictionary": "computed",
        "embed_antenna_map": False}
    assert DyscoCodec.from_dict(codecs[0]) == codecs_obj(codecs[0])


def codecs_obj(entry):
    return DyscoCodec.from_dict(entry)


def test_multi_chunk_with_compressors(tmp_path):
    f = make_fixture(5, 4, 6, 4, 99)
    arr = zarr.create_array(tmp_path / "a.zarr", shape=f.shape, chunks=(2, 10, 3, 4),
                            dtype="complex64", serializer=codec_for("af", f, seed=7),
                            compressors=[Crc32cCodec(), ZstdCodec(level=3)], fill_value=0)
    arr[...] = f.data
    back = zarr.open_array(tmp_path / "a.zarr", mode="r")[...]
    # Each chunk equals the direct encode/decode of the same (padded) slice.
    expected = np.zeros_like(f.data)
    for t0 in range(0, 5, 2):
        for c0 in (0, 3):
            block = np.zeros((2, 10, 3, 4), np.complex64)
            src = f.data[t0:t0 + 2, :, c0:c0 + 3]
            block[: src.shape[0]] = src
            dec = d.decode_data(d.encode_data(block, normalization="af", seed=7, **af(f)),
                                block.shape, **af(f))
            expected[t0:t0 + 2, :, c0:c0 + 3] = dec[: src.shape[0]]
    np.testing.assert_array_equal(back, expected)
    # AF clips autocorrelations (documented); check the cross-correlations.
    cross = np.array(f.antenna1) != np.array(f.antenna2)
    rel = np.linalg.norm((back - f.data)[:, cross]) / np.linalg.norm(f.data[:, cross])
    assert rel < 0.01


def test_sharding(tmp_path):
    f = make_fixture(4, 4, 8, 4, 5)
    inner = codec_for("rf", f)
    arr = zarr.create_array(tmp_path / "s.zarr", shape=f.shape, chunks=(1, 10, 4, 4),
                            shards=(4, 10, 8, 4), dtype="complex64", serializer=inner,
                            compressors=None, fill_value=0)
    arr[...] = f.data
    back = zarr.open_array(tmp_path / "s.zarr", mode="r")[...]
    for t in range(4):
        for c0 in (0, 4):
            blk = np.ascontiguousarray(f.data[t:t + 1, :, c0:c0 + 4])
            dec = d.decode_data(d.encode_data(blk, normalization="rf"), blk.shape)
            np.testing.assert_array_equal(back[t:t + 1, :, c0:c0 + 4], dec)
    meta = json.loads((tmp_path / "s.zarr" / "zarr.json").read_text())
    assert meta["codecs"][0]["name"] == "sharding_indexed"
    assert meta["codecs"][0]["configuration"]["codecs"][0]["name"] == "dp3core.dysco"


def test_nan_round_trip(tmp_path):
    f = make_fixture(2, 3, 2, 2, 8)
    data = f.data.copy()
    data[0, 1, 0, 0] = np.nan
    arr = zarr.create_array(tmp_path / "a.zarr", shape=data.shape, chunks=data.shape,
                            dtype="complex64", serializer=codec_for("row", f), compressors=None)
    arr[...] = data
    back = arr[...]
    assert np.isnan(back[0, 1, 0, 0].real)
    assert np.isfinite(back[0, 1, 0, 1])


def test_validation(tmp_path, golden_fixture):
    f = golden_fixture
    with pytest.raises(ValueError, match="whole baseline"):
        zarr.create_array(tmp_path / "x1.zarr", shape=f.shape, chunks=(1, 5, 4, 4),
                          dtype="complex64", serializer=codec_for("af", f))
    with pytest.raises(ValueError, match="whole polarization"):
        zarr.create_array(tmp_path / "x2.zarr", shape=f.shape, chunks=(1, 15, 4, 2),
                          dtype="complex64", serializer=codec_for("row", f))
    with pytest.raises(ValueError, match="needs dtype complex64"):
        zarr.create_array(tmp_path / "x3.zarr", shape=f.shape, chunks=f.shape,
                          dtype="complex128", serializer=codec_for("row", f))
    with pytest.raises(ValueError, match="4-dimensional"):
        zarr.create_array(tmp_path / "x4.zarr", shape=(4, 4), chunks=(4, 4),
                          dtype="complex64", serializer=codec_for("row", f))
    with pytest.raises(ValueError, match="requires n_antennas"):
        DyscoCodec(normalization="af")
    with pytest.raises(ValueError, match="not in"):
        DyscoCodec(normalization="af", antenna1=[0, 5], antenna2=[1, 1], n_antennas=5)
    with pytest.raises(NotImplementedError, match="transpose"):
        DyscoCodec(normalization="row", axes=["baseline", "time", "frequency", "polarization"])
    with pytest.raises(ValueError, match="unknown"):
        DyscoCodec.from_dict({"name": "dp3core.dysco", "configuration": {"normalization": "row", "x": 1}})
    with pytest.raises(ValueError, match="format_version"):
        DyscoCodec.from_dict({"name": "dp3core.dysco", "configuration": {"normalization": "row",
                                                                         "format_version": 2}})
    # RF may split baselines (design section 6).
    zarr.create_array(tmp_path / "ok.zarr", shape=f.shape, chunks=(1, 5, 4, 4),
                      dtype="complex64", serializer=codec_for("rf", f))


def test_seed_as_string_and_embedded_dictionary(tmp_path, golden_fixture):
    f = golden_fixture
    c = DyscoCodec.from_dict({"name": "dp3core.dysco", "configuration": {
        "normalization": "row", "seed": str(GOLDEN_SEED), "dictionary": "embedded"}})
    assert c.config.seed == GOLDEN_SEED
    arr = zarr.create_array(tmp_path / "e.zarr", shape=f.shape, chunks=f.shape, dtype="complex64",
                            serializer=c, compressors=None)
    arr[...] = f.data
    # Same symbols as the computed-dictionary golden chunk, so same decoded values.
    assert _core.xxh3(arr[...]) == GOLDEN["row"][1]
    h = d.read_header((tmp_path / "e.zarr" / "c" / "0" / "0" / "0" / "0").read_bytes())
    assert h["dictionary_embedded"]


def test_encoded_size_matches(golden_fixture):
    f = golden_fixture
    from zarr.core.array_spec import ArrayConfig, ArraySpec
    from zarr.core.buffer import default_buffer_prototype
    from zarr.core.dtype import parse_data_type
    spec = ArraySpec(shape=f.shape, dtype=parse_data_type("complex64", zarr_format=3),
                     fill_value=0, config=ArrayConfig.from_dict({}),
                     prototype=default_buffer_prototype())
    c = codec_for("af", f)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        size = c.compute_encoded_size(f.data.nbytes, spec)
    assert size == len(d.encode_data(f.data, **af(f)))
