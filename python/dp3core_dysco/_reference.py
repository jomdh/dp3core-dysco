"""Pure-NumPy reference decoder for dp3core.dysco / dp3core.dysco-weights chunks.

Written from spec/SPEC.md only (sections 2-6, the decode formulas of sections 8
and 9, and the XXH3-64 function the spec names for ``dict_checksum``). It uses
nothing but the standard library and NumPy and never imports the compiled
extension, so the tests can use it to show that the specification is sufficient
to read the format. It is a test aid, not a user API: it is slow and it does not
encode.

Convention notice: the codec names ``dp3core.dysco`` and ``dp3core.dysco-weights``,
their configuration fields and the chunk layout are unofficial DP3Core conventions,
not endorsed by LOFAR, ASTRON or the MSv4 schema group.
"""
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import functools
import struct
from collections.abc import Sequence

import numpy as np

__all__ = [
    "ReferenceFormatError",
    "decode_data",
    "decode_table",
    "decode_weights",
    "read_header",
    "table_checksum",
    "xxh3_64",
]


class ReferenceFormatError(ValueError):
    """A chunk violates SPEC.md section 5."""


# --------------------------------------------------------------------------- XXH3
# XXH3-64 with seed 0 (xxHash 0.8.x), the checksum SPEC.md section 4 names.
_M64 = (1 << 64) - 1
_P32_1, _P32_2, _P32_3 = 0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D
_P64_1, _P64_2, _P64_3 = 0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9
_P64_4, _P64_5 = 0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5
_PMX1, _PMX2 = 0x165667919E3779F9, 0x9FB21C651E98DF25
_SECRET = bytes.fromhex(
    "b8fe6c3923a44bbe7c01812cf721ad1cded46de9839097db7240a4a4b7b3671f"
    "cb79e64eccc0e578825ad07dccff7221b8084674f743248ee03590e6813a264c"
    "3c2852bb91c300cb88d0658b1b532ea371644897a20df94e3819ef46a9deacd8"
    "a8fa763fe39c343ff9dcbbc7c70b4f1d8a51e04bcdb45931c89f7ec9d9787364"
    "eac5ac8334d3ebc3c581a0fffa1363eb170ddd51b7f0da49d316552629d4689e"
    "2b16be587d47a1fc8ff8b8d17ad031ce45cb3a8f95160428afd7fbcabb4b407e")


def _r64(b: bytes | memoryview, o: int) -> int:
    return int.from_bytes(b[o:o + 8], "little")


def _r32(b: bytes | memoryview, o: int) -> int:
    return int.from_bytes(b[o:o + 4], "little")


def _rotl64(x: int, r: int) -> int:
    return ((x << r) | (x >> (64 - r))) & _M64


def _fold(a: int, b: int) -> int:
    p = a * b
    return (p & _M64) ^ (p >> 64)


def _avalanche(h: int) -> int:
    h ^= h >> 37
    h = (h * _PMX1) & _M64
    return h ^ (h >> 32)


def _xxh64_avalanche(h: int) -> int:
    h ^= h >> 33
    h = (h * _P64_2) & _M64
    h ^= h >> 29
    h = (h * _P64_3) & _M64
    return h ^ (h >> 32)


def _mix16(b, o: int, so: int) -> int:
    return _fold(_r64(b, o) ^ _r64(_SECRET, so), _r64(b, o + 8) ^ _r64(_SECRET, so + 8))


def xxh3_64(data: bytes | bytearray | memoryview) -> int:
    """XXH3_64bits(data) with seed 0 and the default secret."""
    b = bytes(data)
    n = len(b)
    s = _SECRET
    if n == 0:
        return _xxh64_avalanche(_r64(s, 56) ^ _r64(s, 64))
    if n <= 3:
        combined = (b[0] << 16) | (b[n >> 1] << 24) | b[n - 1] | (n << 8)
        return _xxh64_avalanche(combined ^ (_r32(s, 0) ^ _r32(s, 4)))
    if n <= 8:
        v = (_r32(b, n - 4) + (_r32(b, 0) << 32)) ^ (_r64(s, 8) ^ _r64(s, 16))
        v ^= _rotl64(v, 49) ^ _rotl64(v, 24)
        v = (v * _PMX2) & _M64
        v ^= (v >> 35) + n
        v = (v * _PMX2) & _M64
        return v ^ (v >> 28)
    if n <= 16:
        lo = _r64(b, 0) ^ (_r64(s, 24) ^ _r64(s, 32))
        hi = _r64(b, n - 8) ^ (_r64(s, 40) ^ _r64(s, 48))
        swapped = int.from_bytes(lo.to_bytes(8, "little"), "big")
        return _avalanche((n + swapped + hi + _fold(lo, hi)) & _M64)
    if n <= 128:
        acc = n * _P64_1
        if n > 32:
            if n > 64:
                if n > 96:
                    acc += _mix16(b, 48, 96) + _mix16(b, n - 64, 112)
                acc += _mix16(b, 32, 64) + _mix16(b, n - 48, 80)
            acc += _mix16(b, 16, 32) + _mix16(b, n - 32, 48)
        acc += _mix16(b, 0, 0) + _mix16(b, n - 16, 16)
        return _avalanche(acc & _M64)
    if n <= 240:
        acc = n * _P64_1
        for i in range(8):
            acc += _mix16(b, 16 * i, 16 * i)
        acc = _avalanche(acc & _M64)
        for i in range(8, n // 16):
            acc += _mix16(b, 16 * i, 16 * (i - 8) + 3)
        acc += _mix16(b, n - 16, 136 - 17)
        return _avalanche(acc & _M64)
    return _xxh3_long(b)


def _xxh3_long(b: bytes) -> int:
    n = len(b)
    acc = [_P32_3, _P64_1, _P64_2, _P64_3, _P64_4, _P32_2, _P64_5, _P32_1]
    stripes_per_block = (len(_SECRET) - 64) // 8
    block_len = 64 * stripes_per_block
    lanes = np.frombuffer(b, dtype="<u8", count=n // 8)
    key = [_r64(_SECRET, o) for o in range(0, len(_SECRET) - 7)]

    def stripe(offset: int, secret_offset: int) -> None:
        if offset % 8 == 0:
            vals = [int(v) for v in lanes[offset // 8: offset // 8 + 8]]
        else:
            vals = [_r64(b, offset + 8 * i) for i in range(8)]
        for i in range(8):
            v = vals[i]
            k = v ^ key[secret_offset + 8 * i]
            acc[i ^ 1] = (acc[i ^ 1] + v) & _M64
            acc[i] = (acc[i] + (k & 0xFFFFFFFF) * (k >> 32)) & _M64

    n_blocks = (n - 1) // block_len
    for blk in range(n_blocks):
        for st in range(stripes_per_block):
            stripe(blk * block_len + 64 * st, 8 * st)
        for i in range(8):
            a = acc[i]
            a ^= a >> 47
            a ^= key[len(_SECRET) - 64 + 8 * i]
            acc[i] = (a * _P32_1) & _M64
    for st in range(((n - 1) - block_len * n_blocks) // 64):
        stripe(n_blocks * block_len + 64 * st, 8 * st)
    stripe(n - 64, len(_SECRET) - 64 - 7)
    result = n * _P64_1
    for i in range(4):
        result += _fold(acc[2 * i] ^ key[11 + 16 * i], acc[2 * i + 1] ^ key[11 + 16 * i + 8])
    return _avalanche(result & _M64)


# ---------------------------------------------------------------- tables (SPEC 4)
# All arithmetic is element-wise binary64 NumPy (one rounding per operation, no
# FMA), in exactly the order SPEC.md writes it.
_LN2_HI = 6.93147180369123816490e-01
_LN2_LO = 1.90821492927058770002e-10
_INV_LN2 = 1.44269504088896338700
_INV_SQRT_PI = 5.64189583547756286948e-01
_INV_SQRT2 = 7.07106781186547524401e-01


def _exp(x: np.ndarray) -> np.ndarray:
    """SPEC 4.1, for x <= 0."""
    out = np.zeros_like(x)
    ok = x >= -745.0
    xs = x[ok]
    k = xs * _INV_LN2
    nk = -np.trunc(-k + 0.5)
    r = (xs - nk * _LN2_HI) - nk * _LN2_LO
    p = np.ones_like(xs)
    for i in range(27, 0, -1):
        p = 1.0 + (r / float(i)) * p
    out[ok] = np.ldexp(p, nk.astype(np.int64))
    return out


def _erfc(z: np.ndarray) -> np.ndarray:
    """SPEC 4.2, for z >= 0."""
    out = np.empty_like(z)
    small = z < 1.0
    zs = z[small]
    if zs.size:
        z2 = zs * zs
        t = zs.copy()
        s = zs.copy()
        for j in range(1, 40):
            t = (t * (2.0 * z2)) / float(2 * j + 1)
            s = s + t
        out[small] = 1.0 - ((2.0 * _INV_SQRT_PI) * _exp(-z2)) * s
    for sel, depth in ((~small & (z < 2.0), 250), ((z >= 2.0) & (z < 4.0), 80), (z >= 4.0, 30)):
        zl = z[sel]
        if not zl.size:
            continue
        t = zl.copy()
        for j in range(depth, 0, -1):
            t = zl + (0.5 * float(j)) / t
        out[sel] = (_INV_SQRT_PI * _exp(-(zl * zl))) / t
    return out


def _phi_lower(x: np.ndarray) -> np.ndarray:
    return 0.5 * _erfc((-x) * _INV_SQRT2)


def _inverse_phi_lower(p: np.ndarray) -> np.ndarray:
    lo = np.full_like(p, -16.0)
    hi = np.zeros_like(p)
    for _ in range(64):
        mid = 0.5 * (lo + hi)
        below = _phi_lower(mid) < p
        lo = np.where(below, mid, lo)
        hi = np.where(below, hi, mid)
    return np.where(p == 0.5, 0.0, 0.5 * (lo + hi))


_DISTRIBUTIONS = {0: "gaussian", 1: "truncated_gaussian", 2: "uniform"}
_NORMALIZATIONS = {0: "af", 1: "rf", 2: "row"}
_BITS = (2, 3, 4, 6, 8, 10, 12, 16)


@functools.lru_cache(maxsize=None)
def _table(bits: int, distribution: str, truncation: float) -> np.ndarray:
    n = (1 << bits) - 1
    m = (n - 1) // 2
    sqrt3 = float(np.float32(1.7320508075688772))
    i = np.arange(m, dtype=np.float64)
    a = 2.0 * i + 1.0  # exact
    b = float(2 * n)
    if distribution == "gaussian":
        half = sqrt3 * _inverse_phi_lower(a / b)
    elif distribution == "uniform":
        half = (sqrt3 * (a - float(n))) / float(n)
    elif distribution == "truncated_gaussian":
        if not 0.0 < truncation <= 8.0:
            raise ValueError(f"truncation must be in (0, 8], got {truncation}")
        t = float(_phi_lower(np.array([-truncation]))[0])
        w = 1.0 - 2.0 * t
        half = _inverse_phi_lower(t + (a / b) * w)
    else:
        raise ValueError(f"unknown distribution {distribution!r}")
    half32 = half.astype(np.float32)
    table = np.concatenate([half32, np.zeros(1, np.float32), -half32[::-1]])
    table.setflags(write=False)
    return table


def decode_table(bits: int, distribution: str = "truncated_gaussian",
                 truncation: float = 2.5) -> np.ndarray:
    """The (2**bits - 1) float32 decode levels of SPEC.md section 4."""
    if bits not in _BITS:
        raise ValueError(f"unsupported bit count {bits}")
    return _table(bits, distribution, float(truncation) if distribution == "truncated_gaussian" else 0.0)


def table_checksum(table: np.ndarray) -> int:
    return xxh3_64(np.asarray(table, dtype="<f4").tobytes())


# ---------------------------------------------------------------- header (SPEC 5)
_HEADER = struct.Struct("<8sHHBBBBIIIIIIdQQIIQ")
_FLAG_DICT, _FLAG_MAP = 1, 2


def _fail(msg: str):
    raise ReferenceFormatError("dp3core.dysco chunk: " + msg)


def read_header(buf) -> dict:
    """Parses and checks the header and sizes (SPEC 5.1-5.3)."""
    b = memoryview(buf).cast("B")
    if len(b) < 12:
        _fail("too short for a header")
    if bytes(b[:8]) != b"DP3DYSCO":
        _fail("bad magic")
    version, header_bytes = struct.unpack_from("<HH", b, 8)
    if version != 1:
        _fail(f"unknown format version {version}")
    if header_bytes < 80:
        _fail("header_bytes < 80")
    if len(b) < header_bytes:
        _fail("buffer shorter than the header")
    (_, _, _, kind, bits, dist, norm, flags, nt, nb, nf, npol, na, trunc, nonce, dsum, m_side,
     y_sym, reserved) = _HEADER.unpack_from(b, 0)
    if kind not in (1, 2):
        _fail("invalid kind")
    data = kind == 1
    if bits not in _BITS:
        _fail("invalid bits")
    if data:
        if dist not in _DISTRIBUTIONS or norm not in _NORMALIZATIONS:
            _fail("invalid distribution or normalization code")
    elif dist != 255 or norm != 255:
        _fail("weights chunk needs distribution and normalization 255")
    af = data and _NORMALIZATIONS[norm] == "af"
    if flags & ~(_FLAG_DICT | _FLAG_MAP):
        _fail("unknown flag bits")
    if (flags & _FLAG_DICT) and not data:
        _fail("dictionary flag on a weights chunk")
    if (flags & _FLAG_MAP) and not af:
        _fail("antenna-map flag without AF")
    if 0 in (nt, nb, nf, npol):
        _fail("zero dimension")
    if (na == 0) if af else (na != 0):
        _fail("n_antenna must be >= 1 for AF and 0 otherwise")
    truncated = data and _DISTRIBUTIONS[dist] == "truncated_gaussian"
    if truncated:
        if not 0.0 < trunc <= 8.0:
            _fail("invalid truncation")
    elif struct.pack("<d", trunc) != bytes(8):
        _fail("truncation must be +0.0")
    if not data and dsum != 0:
        _fail("weights chunk must have dict_checksum 0")
    if reserved != 0:
        _fail("reserved field is not 0")
    if not data:
        exp_m = 1
    elif af:
        exp_m = npol * (nf + na)
    elif _NORMALIZATIONS[norm] == "rf":
        exp_m = npol * (nf + nb)
    else:
        exp_m = nb
    n_sym = (2 * nb * nf * npol) if data else nb * nf
    exp_y = (n_sym * bits + 7) // 8
    if m_side != exp_m or y_sym != exp_y:
        _fail("side_floats_per_block or symbol_bytes_per_block inconsistent")
    dict_bytes = 4 * ((1 << bits) - 1) if flags & _FLAG_DICT else 0
    map_bytes = 8 * nb if flags & _FLAG_MAP else 0
    size = header_bytes + dict_bytes + map_bytes + nt * (4 * m_side + y_sym)
    if len(b) != size:
        _fail(f"length {len(b)} != encoded size {size}")
    return dict(
        kind="data" if data else "weights", bits=bits,
        distribution=_DISTRIBUTIONS[dist] if data else None,
        normalization=_NORMALIZATIONS[norm] if data else None,
        truncation=trunc, n_antennas=na, flags=flags, shape=(nt, nb, nf, npol),
        nonce=nonce, dict_checksum=dsum, header_bytes=header_bytes,
        side_floats_per_block=m_side, symbol_bytes_per_block=y_sym,
        dictionary_bytes=dict_bytes, antenna_map_bytes=map_bytes, encoded_size=size)


def _check_shape(h: dict, kind: str, shape: Sequence[int]) -> None:
    if h["kind"] != kind:
        _fail(f"expected a {kind} chunk, got {h['kind']}")
    if tuple(int(s) for s in shape) != h["shape"]:
        _fail(f"shape {tuple(shape)} differs from the header's {h['shape']}")


def _unpack(block: np.ndarray, count: int, bits: int) -> np.ndarray:
    """SPEC 6: LSB-first bit stream -> symbols (int64)."""
    stream = np.unpackbits(block, bitorder="little")[: count * bits].reshape(count, bits)
    return stream.astype(np.int64) @ (np.int64(1) << np.arange(bits, dtype=np.int64))


def _blocks(b: np.ndarray, start: int, h: dict):
    m, y = h["side_floats_per_block"], h["symbol_bytes_per_block"]
    for t in range(h["shape"][0]):
        o = start + t * (4 * m + y)
        yield b[o:o + 4 * m].view("<f4").astype(np.float32), b[o + 4 * m:o + 4 * m + y]


# ---------------------------------------------------------------- decode (SPEC 8, 9)
def decode_data(buf, shape: Sequence[int], antenna1: Sequence[int] | None = None,
                antenna2: Sequence[int] | None = None,
                n_antennas: int | None = None) -> np.ndarray:
    """Decodes a dp3core.dysco chunk to complex64 (time, baseline, frequency, pol)."""
    h = read_header(buf)
    _check_shape(h, "data", shape)
    b = np.frombuffer(memoryview(buf).cast("B"), dtype=np.uint8)
    nt, nb, nf, npol = h["shape"]
    bits = h["bits"]
    pos = h["header_bytes"]
    if h["flags"] & _FLAG_DICT:
        levels = b[pos:pos + h["dictionary_bytes"]].view("<f4").astype(np.float32)
        pos += h["dictionary_bytes"]
        if table_checksum(levels) != h["dict_checksum"]:
            _fail("embedded dictionary does not match dict_checksum")
    else:
        levels = decode_table(bits, h["distribution"], h["truncation"])
        if table_checksum(levels) != h["dict_checksum"]:
            _fail("dict_checksum differs from this reader's table")
    levels = np.append(levels, np.float32("nan")).astype(np.float64)  # symbol Q-1 -> NaN

    af = h["normalization"] == "af"
    given = antenna1 is not None
    if given:
        a1 = np.asarray(antenna1, dtype=np.int64)
        a2 = np.asarray(antenna2, dtype=np.int64)
        na = int(n_antennas)
    if h["flags"] & _FLAG_MAP:
        amap = b[pos:pos + h["antenna_map_bytes"]].view("<u4").reshape(nb, 2).astype(np.int64)
        pos += h["antenna_map_bytes"]
        if given and (na != h["n_antennas"] or not np.array_equal(a1, amap[:, 0])
                      or not np.array_equal(a2, amap[:, 1])):
            _fail("embedded antenna map differs from the given one")
        a1, a2, na = amap[:, 0], amap[:, 1], h["n_antennas"]
    elif af:
        if not given:
            raise ValueError("decoding an AF chunk needs an antenna map")
        if na != h["n_antennas"]:
            _fail("n_antenna differs from the antenna map's")
    if af:
        if a1.shape != (nb,) or a2.shape != (nb,) or a1.max() >= na or a2.max() >= na or min(a1.min(), a2.min()) < 0:
            raise ValueError("invalid antenna map")

    fp = nf * npol
    pol_of_vis = np.arange(fp) % npol
    out = np.empty((nt, nb, fp, 2), dtype=np.float32)
    for t, (side, block) in enumerate(_blocks(b, pos, h)):
        sym = _unpack(block, 2 * nb * fp, bits).reshape(nb, fp, 2)
        side64 = side.astype(np.float64)
        if h["normalization"] == "row":
            factor = np.broadcast_to(side64[:, None], (nb, fp))
        elif h["normalization"] == "rf":
            row = side64[fp:].reshape(nb, npol)
            factor = side64[None, :fp] * row[:, pol_of_vis]
        else:
            ant = side64[fp:].reshape(npol, na)
            ant_factor = ant[:, a1].T * ant[:, a2].T  # (nb, npol)
            factor = side64[None, :fp] * ant_factor[:, pol_of_vis]
        out[t] = (levels[sym] * factor[:, :, None]).astype(np.float32)
    return out.view(np.complex64).reshape(nt, nb, nf, npol)


def decode_weights(buf, shape: Sequence[int]) -> np.ndarray:
    """Decodes a dp3core.dysco-weights chunk to float32 (time, baseline, frequency, pol)."""
    h = read_header(buf)
    _check_shape(h, "weights", shape)
    b = np.frombuffer(memoryview(buf).cast("B"), dtype=np.uint8)
    nt, nb, nf, npol = h["shape"]
    q1 = float((1 << h["bits"]) - 1)
    out = np.empty((nt, nb, nf, npol), dtype=np.float32)
    for t, (side, block) in enumerate(_blocks(b, h["header_bytes"], h)):
        scale = float(side[0]) / q1
        values = (_unpack(block, nb * nf, h["bits"]).astype(np.float64) * scale).astype(np.float32)
        out[t] = values.reshape(nb, nf, 1)
    return out
