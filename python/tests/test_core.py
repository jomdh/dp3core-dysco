# SPDX-License-Identifier: GPL-3.0-only
import numpy as np
import pytest
from fixtures import GOLDEN, GOLDEN_SEED, make_fixture

import dp3core_dysco as d
from dp3core_dysco import _core


def af_kwargs(f):
    return dict(antenna1=f.antenna1, antenna2=f.antenna2, n_antennas=f.n_antennas)


def test_fixture_matches_cpp_nonce(golden_fixture):
    # The nonce is XXH3 of the input bytes: equal nonces mean equal inputs.
    enc = d.encode_data(golden_fixture.data, normalization="row")
    assert d.read_header(enc)["nonce"] == _core.xxh3(golden_fixture.data.tobytes())


@pytest.mark.parametrize("norm", ["row", "rf", "af"])
@pytest.mark.parametrize("threads", [1, 3, 8])
def test_golden_data(golden_fixture, norm, threads):
    f = golden_fixture
    enc = d.encode_data(f.data, normalization=norm, seed=GOLDEN_SEED, threads=threads, **af_kwargs(f))
    dec = d.decode_data(enc, f.shape, **(af_kwargs(f) if norm == "af" else {}))
    assert (_core.xxh3(enc), _core.xxh3(dec)) == GOLDEN[norm]


def test_golden_weights(golden_fixture):
    f = golden_fixture
    enc = d.encode_weights(f.weights)
    dec = d.decode_weights(enc, f.shape)
    assert (_core.xxh3(enc), _core.xxh3(dec)) == GOLDEN["weights"]


def test_header(golden_fixture):
    f = golden_fixture
    enc = d.encode_data(f.data, bits=8, distribution="uniform", normalization="af",
                        embed_dictionary=True, embed_antenna_map=True, **af_kwargs(f))
    h = d.read_header(enc)
    assert h["kind"] == "data" and h["bits"] == 8 and h["distribution"] == "uniform"
    assert h["normalization"] == "af" and h["n_antennas"] == 5 and h["shape"] == f.shape
    assert h["dictionary_embedded"] and h["antenna_map_embedded"]
    assert h["encoded_size"] == len(enc)
    # Embedded map: decodes without one.
    np.testing.assert_array_equal(d.decode_data(enc, f.shape),
                                  d.decode_data(enc, f.shape, **af_kwargs(f)))


def test_errors(golden_fixture):
    f = golden_fixture
    with pytest.raises(ValueError, match="antenna map"):
        d.encode_data(f.data, normalization="af")
    with pytest.raises(ValueError, match="bit count"):
        d.encode_data(f.data, bits=5, normalization="row")
    with pytest.raises(ValueError, match="normalization"):
        d.encode_data(f.data, normalization="xx")
    enc = d.encode_data(f.data, normalization="row")
    with pytest.raises(d.FormatError):
        d.decode_data(enc[:-1], f.shape)
    with pytest.raises(d.FormatError):
        d.decode_data(enc, (1,) + f.shape[1:])
    assert issubclass(d.FormatError, ValueError)
    with pytest.raises(ValueError, match="negative or not finite"):
        d.encode_weights(-f.weights - 1)


def test_decode_table_is_ascending():
    t = d.decode_table(10)
    assert t.dtype == np.float32 and t.size == 1023 and np.all(np.diff(t) > 0)
