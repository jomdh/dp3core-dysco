# SPDX-License-Identifier: GPL-3.0-only
"""The pure-NumPy reference decoder (design 7.2 / commit 16) against the golden fixtures.

The reference module is loaded from its file under another name so that it runs
without the compiled extension; the tests then use the extension only to produce
chunks and to hash results.
"""
import importlib.util
import re
import sys
from pathlib import Path

import numpy as np
import pytest
from fixtures import GOLDEN, GOLDEN_SEED, make_fixture

import dp3core_dysco as d
from dp3core_dysco import _core

REF_PATH = Path(d.__file__).with_name("_reference.py")
GOLDEN_TABLES_H = Path(__file__).resolve().parents[2] / "core" / "tests" / "golden_tables.h"


@pytest.fixture(scope="module")
def ref():
    spec = importlib.util.spec_from_file_location("dysco_reference_standalone", REF_PATH)
    module = importlib.util.module_from_spec(spec)
    before = set(sys.modules)
    spec.loader.exec_module(module)
    loaded = set(sys.modules) - before
    assert not any("dp3core_dysco" in name for name in loaded), loaded
    return module


def test_reference_is_standalone():
    source = REF_PATH.read_text()
    imports = re.findall(r"^\s*(?:from|import)\s+([\w.]+)", source, flags=re.M)
    assert set(imports) <= {"__future__", "functools", "struct", "collections.abc", "numpy"}, imports


def af_kwargs(f):
    return dict(antenna1=f.antenna1, antenna2=f.antenna2, n_antennas=f.n_antennas)


def bits_equal(a, b):
    return np.array_equal(np.asarray(a).view(np.uint32), np.asarray(b).view(np.uint32))


@pytest.mark.parametrize("norm", ["row", "rf", "af"])
def test_golden_data(ref, golden_fixture, norm):
    f = golden_fixture
    enc = d.encode_data(f.data, normalization=norm, seed=GOLDEN_SEED, **af_kwargs(f))
    assert _core.xxh3(enc) == GOLDEN[norm][0]
    dec = ref.decode_data(enc, f.shape, **(af_kwargs(f) if norm == "af" else {}))
    assert dec.dtype == np.complex64 and dec.shape == f.shape
    assert _core.xxh3(dec) == GOLDEN[norm][1]
    assert ref.xxh3_64(dec.tobytes()) == GOLDEN[norm][1]
    h = ref.read_header(enc)
    assert h["nonce"] == ref.xxh3_64(f.data.tobytes())  # SPEC 7 nonce, recomputed


def test_golden_weights(ref, golden_fixture):
    f = golden_fixture
    enc = d.encode_weights(f.weights)
    assert _core.xxh3(enc) == GOLDEN["weights"][0]
    dec = ref.decode_weights(enc, f.shape)
    assert dec.dtype == np.float32 and _core.xxh3(dec) == GOLDEN["weights"][1]


def test_golden_table_checksums(ref):
    if not GOLDEN_TABLES_H.exists():
        pytest.skip("core/tests/golden_tables.h not available (not a source checkout)")
    names = {"kGaussian": "gaussian", "kTruncatedGaussian": "truncated_gaussian", "kUniform": "uniform"}
    rows = re.findall(r"\{(\d+), dp3core::dysco::Distribution::(\w+), ([\d.]+), (0x[0-9A-Fa-f]+)ull\}",
                      GOLDEN_TABLES_H.read_text())
    assert len(rows) >= 24
    for bits, dist, trunc, checksum in rows:
        table = ref.decode_table(int(bits), names[dist], float(trunc))
        assert ref.table_checksum(table) == int(checksum, 16), (bits, dist, trunc)


@pytest.mark.parametrize("bits", [2, 3, 4, 6, 8, 10, 12, 16])
@pytest.mark.parametrize("distribution", ["gaussian", "truncated_gaussian", "uniform"])
def test_tables_equal_core(ref, bits, distribution):
    assert bits_equal(ref.decode_table(bits, distribution, 2.5), _core.decode_table(bits, distribution, 2.5))


@pytest.mark.parametrize("trunc", [0.75, 1.5, 3.0, 4.0, 8.0])
def test_truncations_equal_core(ref, trunc):
    assert bits_equal(ref.decode_table(10, "truncated_gaussian", trunc),
                      _core.decode_table(10, "truncated_gaussian", trunc))


def test_xxh3_equals_core(ref):
    rng = np.random.default_rng(7)
    for n in [*range(0, 260), 511, 1023, 1024, 1025, 4096, 65535 * 4]:
        data = rng.integers(0, 256, n, dtype=np.uint8).tobytes()
        assert ref.xxh3_64(data) == _core.xxh3(data), n


CASES = [
    dict(bits=2, distribution="uniform", normalization="row"),
    dict(bits=3, distribution="gaussian", normalization="rf"),
    dict(bits=6, distribution="truncated_gaussian", truncation=1.5, normalization="af"),
    dict(bits=12, normalization="rf", embed_dictionary=True),
    dict(bits=16, distribution="gaussian", normalization="af", embed_antenna_map=True),
    dict(bits=8, normalization="af", embed_dictionary=True, embed_antenna_map=True),
]


@pytest.mark.parametrize("case", CASES, ids=lambda c: "-".join(f"{v}" for v in c.values()))
def test_matches_core_bitwise(ref, case):
    f = make_fixture(4, 7, 5, 2, 99)
    data = f.data.copy()
    data[1, 3, 2, 1] = complex(np.nan, 1.0)
    data[2, 5] = np.nan  # a whole row not finite
    data[3, 1, 0, 0] = complex(np.inf, -np.inf)
    af = case["normalization"] == "af"
    enc = d.encode_data(data, seed=5, **case, **(af_kwargs(f) if af else {}))
    kw = af_kwargs(f) if af and not case.get("embed_antenna_map") else {}
    assert bits_equal(ref.decode_data(enc, data.shape, **kw).view(np.float32),
                      d.decode_data(enc, data.shape, **kw).view(np.float32))


@pytest.mark.parametrize("bits", [2, 3, 4, 6, 8, 10, 12, 16])
def test_weights_match_core(ref, bits):
    f = make_fixture(2, 4, 3, 4, 3)
    w = f.weights.copy()
    w[1] = 0.0  # an all-zero block
    enc = d.encode_weights(w, bits=bits)
    assert bits_equal(ref.decode_weights(enc, w.shape), d.decode_weights(enc, w.shape))


def test_header_matches_core(ref, golden_fixture):
    f = golden_fixture
    enc = d.encode_data(f.data, bits=8, distribution="uniform", normalization="af",
                        embed_dictionary=True, embed_antenna_map=True, **af_kwargs(f))
    mine, core = ref.read_header(enc), d.read_header(enc)
    for key in ("kind", "bits", "distribution", "normalization", "truncation", "n_antennas",
                "shape", "nonce", "dict_checksum", "side_floats_per_block",
                "symbol_bytes_per_block", "encoded_size", "header_bytes"):
        assert mine[key] == core[key], key


def _patch(buf, offset, fmt, value):
    import struct
    b = bytearray(buf)
    struct.pack_into(fmt, b, offset, value)
    return bytes(b)


def test_rejects_bad_chunks(ref, golden_fixture):
    f = golden_fixture
    enc = d.encode_data(f.data, normalization="row")
    bad = {
        "magic": _patch(enc, 0, "<B", ord("X")),
        "version": _patch(enc, 8, "<H", 2),
        "header_bytes": _patch(enc, 10, "<H", 79),
        "kind": _patch(enc, 12, "<B", 3),
        "bits": _patch(enc, 13, "<B", 5),
        "distribution": _patch(enc, 14, "<B", 3),
        "flags": _patch(enc, 16, "<I", 4),
        "map flag without af": _patch(enc, 16, "<I", 2),
        "zero dim": _patch(enc, 20, "<I", 0),
        "n_antenna": _patch(enc, 36, "<I", 3),
        "truncation": _patch(enc, 40, "<d", 9.0),
        "side": _patch(enc, 64, "<I", 1),
        "reserved": _patch(enc, 72, "<Q", 1),
        "checksum": _patch(enc, 56, "<Q", 1),
        "short": enc[:-1],
        "long": enc + b"\0",
        "tiny": enc[:5],
    }
    for name, chunk in bad.items():
        with pytest.raises(ref.ReferenceFormatError):
            ref.decode_data(chunk, f.shape)
        with pytest.raises(d.FormatError):  # the core agrees
            d.decode_data(chunk, f.shape)
    with pytest.raises(ref.ReferenceFormatError, match="shape"):
        ref.decode_data(enc, (1, *f.shape[1:]))
    with pytest.raises(ref.ReferenceFormatError, match="weights"):
        ref.decode_weights(enc, f.shape)
    af = d.encode_data(f.data, normalization="af", **af_kwargs(f))
    with pytest.raises(ValueError, match="antenna map"):
        ref.decode_data(af, f.shape)


def test_header_extension_is_skipped(ref, golden_fixture):
    # SPEC 5.1: readers skip bytes 80 .. header_bytes-1. The core only writes 80.
    f = golden_fixture
    enc = d.encode_weights(f.weights)
    longer = _patch(enc[:80], 10, "<H", 88) + b"\xAA" * 8 + enc[80:]
    np.testing.assert_array_equal(ref.decode_weights(longer, f.shape), d.decode_weights(enc, f.shape))
