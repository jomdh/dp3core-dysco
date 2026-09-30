# `dp3core.dysco` and `dp3core.dysco-weights`: format and algorithm specification

Version: **format 1, draft**. Status: sections 1–5 are implemented and normative; sections 6–9 are normative drafts that become final when implemented (phase P1).

> **Convention notice.** The codec names, every configuration field, the chunk byte layout and this document are **unofficial DP3Core conventions**. They are not endorsed by LOFAR, ASTRON or the MSv4 schema group.

The method is Dysco (A. R. Offringa, A&A 595, A99, 2016). This library is a port of `aroffringa/dysco` @ `aa30453` (GPL version 3); see `PROVENANCE.md`. The key words MUST, SHOULD and MAY are used as in RFC 2119.

## 1. Scope
- `dp3core.dysco` encodes a `complex64` array of visibilities into bytes (a Zarr v3 array→bytes codec).
- `dp3core.dysco-weights` encodes a `float32` array of weights into bytes.
- The codecs see one chunk at a time. They cannot read other arrays. The writer MUST replace flagged visibilities with NaN before encoding.

## 2. Data model
- A chunk has four axes. In the codec's axis order they are **(time, baseline, frequency, polarization)**, in C order (polarization fastest). The configuration field `axes` names which array axis plays each role, after any array→array codecs.
- Sizes: `T` (time), `B` (baseline), `F` (frequency), `P` (polarization). All MUST be ≥ 1 and ≤ 2^32 − 1.
- A **block** is one time slot of the chunk: `B × F × P` values. Blocks are encoded independently. A chunk is `T` consecutive blocks.
- `bits` ∈ {2, 3, 4, 6, 8, 10, 12, 16}. `Q = 2^bits`. For data, symbols `0 … Q−2` are quantization levels and symbol `Q−1` means "not finite" (decodes to NaN).

## 3. Numeric rules (both codecs)
- All floating-point arithmetic that affects the encoded bytes or the decoded values uses IEEE-754 binary64 (`double`) or binary32 (`float`) with round-to-nearest-even. Only +, −, ×, ÷, `sqrt`, comparisons, negation, and exact scaling by powers of two are used. No fused multiply-add. No reassociation.
- `float(x)` means rounding a double to the nearest binary32.
- Conversions from integers to double are exact in every place they are used.
- Implementations MUST compile the core without FMA contraction or value-changing optimisation (for GCC/Clang: `-ffp-contract=off`, no `-ffast-math`). 32-bit x87 arithmetic is not allowed.

## 4. Decode tables ("dictionaries")
The table for `(bits, distribution, truncation)` has `n = Q − 1` entries `L[0] < … < L[n−1]` (float32). `n` is odd; `m = (n−1)/2`. `L[m] = +0.0`, and `L[n−1−i] = −L[i]`. Only `L[0] … L[m−1]` are computed; the rest follow by symmetry.

### 4.1 Exp
`Exp(x)` for `x ≤ 0`: if `x < −745` return 0. Otherwise
```
k  = x × 1.44269504088896338700              (1/ln 2)
nk = −trunc(−k + 0.5)                         (an integer)
r  = (x − nk × 6.93147180369123816490e-01) − nk × 1.90821492927058770002e-10
p  = 1;  for i = 27 down to 1:  p = 1 + (r / i) × p
return p × 2^nk                               (exact scaling)
```

### 4.2 Erfc
`Erfc(z)` for `z ≥ 0`, with `c = 5.64189583547756286948e-01` (1/√π) and `z2 = z × z`:
- If `z < 1`: `t = z; s = z; for j = 1 … 39: t = (t × (2 × z2)) / (2j + 1); s = s + t`. Return `1 − ((2 × c) × Exp(−z2)) × s`.
- Else: `D = 250` if `z < 2`, `80` if `z < 4`, else `30`. `t = z; for j = D down to 1: t = z + (0.5 × j) / t`. Return `(c × Exp(−z2)) / t`.

### 4.3 Φ and Φ⁻¹ (lower tail)
- `PhiLower(x)` for `x ≤ 0` = `0.5 × Erfc((−x) × 7.07106781186547524401e-01)`.
- `InversePhiLower(p)` for `0 < p ≤ 0.5`: if `p = 0.5` return 0. Else `lo = −16, hi = 0`; 64 times: `mid = 0.5 × (lo + hi)`; if `PhiLower(mid) < p` then `lo = mid` else `hi = mid`. Return `0.5 × (lo + hi)`.

### 4.4 Levels, for `i = 0 … m−1`
Let `S = float(1.7320508075688772)` as a double (Dysco keeps √3 in a float), `a = double(2i + 1)`, `b = double(2n)`.
- **gaussian:** `L[i] = float(S × InversePhiLower(a / b))`.
- **uniform:** `L[i] = float((S × (double(2i + 1) − double(n))) / double(n))`.
- **truncated_gaussian** (truncation `k`, `0 < k ≤ 8`): `t = PhiLower(−k)`, `w = 1 − 2 × t`. `L[i] = float(InversePhiLower(t + (a / b) × w))`.

**Checksum.** `dict_checksum` = XXH3-64 (seed 0, xxHash 0.8.x) of the `n` levels serialized as float32 little-endian.

**Conformance.** Test vectors: `core/tests/golden_tables.h` lists the checksum of every (bits × distribution) table with truncation 2.5, and of 10-bit truncated tables with truncation 1.5, 2, 3 and 4. On x86_64 (glibc, GSL 2.8, 64-bit-mantissa `long double`) every level of these tables is bit-identical to the table built by Dysco's own code (`core/tests/test_tables_gsl.cc`).

## 5. Chunk layout
```
Chunk := Header | [Dictionary] | [AntennaMap] | Block[0] … Block[T−1]
Block := SideData (M × float32 LE) | Symbols (Y bytes)
```
All integers and floats are little-endian.

### 5.1 Header (80 bytes in format 1)
| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | 8 bytes | magic | ASCII `DP3DYSCO` |
| 8 | u16 | format_version | 1. A reader MUST reject a version it does not know |
| 10 | u16 | header_bytes | ≥ 80. Readers MUST skip bytes 80 … header_bytes−1 (reserved for later minor additions) |
| 12 | u8 | kind | 1 data, 2 weights |
| 13 | u8 | bits | §2 |
| 14 | u8 | distribution | data: 0 gaussian, 1 truncated_gaussian, 2 uniform. weights: 255 |
| 15 | u8 | normalization | data: 0 af, 1 rf, 2 row. weights: 255 |
| 16 | u32 | flags | bit 0: dictionary embedded (data only). bit 1: antenna map embedded (AF data only). Other bits MUST be 0 |
| 20 | u32 | n_time (T) | ≥ 1 |
| 24 | u32 | n_baseline (B) | ≥ 1 |
| 28 | u32 | n_channel (F) | ≥ 1 |
| 32 | u32 | n_pol (P) | ≥ 1 |
| 36 | u32 | n_antenna (A) | ≥ 1 for AF, else 0 |
| 40 | f64 | truncation | truncated_gaussian: `0 < k ≤ 8`. Otherwise +0.0 |
| 48 | u64 | nonce | the dither nonce (§7); readers ignore it |
| 56 | u64 | dict_checksum | data: the checksum of the table (§4), also when embedded. weights: 0 |
| 64 | u32 | side_floats_per_block (M) | MUST equal §5.2 |
| 68 | u32 | symbol_bytes_per_block (Y) | MUST equal §5.2 |
| 72 | u64 | reserved | 0 |

### 5.2 Sizes
- `M`: AF `P × (F + A)`; RF `P × (F + B)`; Row `B`; weights `1`.
- Symbols per block `S`: data `2 × B × F × P`; weights `B × F`. `Y = ceil(S × bits / 8)`.
- `M` and `Y` MUST fit in u32.
- Dictionary section: `4 × (Q − 1)` bytes if flag bit 0, else absent. Antenna-map section: `8 × B` bytes (per baseline: u32 antenna1, u32 antenna2) if flag bit 1, else absent.
- **Encoded size** = `header_bytes + dictionary + antenna map + T × (4M + Y)`. A chunk whose length differs MUST be rejected.

### 5.3 Reader checks
A reader MUST reject: a bad magic; an unknown version; `header_bytes < 80`; a buffer shorter than the header; invalid kind, bits, distribution or normalization codes (including the wrong one of "code" and 255 for the kind); unknown flag bits, or a flag not allowed for the kind/normalization; a zero dimension; `n_antenna` present iff AF violated; an invalid truncation; a non-zero reserved field; `M` or `Y` not equal to §5.2; a wrong total length; a shape different from the one Zarr passes. It MUST compare `dict_checksum` with its own table (or with the embedded one) before decoding.

## 6. Bit packing (draft)
Symbols are packed as one little-endian bit stream per block: symbol `j` occupies bits `j × bits … (j+1) × bits − 1`, where stream bit `q` is bit `q mod 8` of byte `floor(q / 8)`. Unused high bits of the last byte are 0. This equals Dysco's `BytePacker` order.

## 7. Dither (draft)
```
mix(z):  z = z + 0x9E3779B97F4A7C15;  z = (z ^ (z >> 30)) × 0xBF58476D1CE4E5B9;
         z = (z ^ (z >> 27)) × 0x94D049BB133111EB;  return z ^ (z >> 31)    (mod 2^64)
nonce  = XXH3-64 (seed 0) of the chunk's input values as little-endian bytes
key    = mix(seed ^ mix(nonce))
u31(i) = mix(key + i) >> 33
i      = ((((t × B + b) × F + f) × P + p) × 2 + c)      c = 0 real, 1 imaginary
```
For a finite normalized value `v` (as float), let `j` be the first level with `L[j] ≥ v`. If `j = 0` the symbol is 0; if no such level exists the symbol is `n−1`; otherwise with `lo = L[j−1]`, `hi = L[j]`: `mark = float(2^31) × (v − lo) / (hi − lo)` in float, and the symbol is `j` if `mark > float(u31(i))`, else `j−1`. Non-finite values get symbol `Q−1`.

## 8. Normalizations (draft)
Ported from Dysco (`rowtimeblockencoder.cc`, `rftimeblockencoder.cc`, `aftimeblockencoder.cc` @ aa30453). The reference implementation is `core/src/normalize.cc`; where this text and that code differ, the code (which is verified bit-identical to Dysco) is authoritative until this section becomes normative.

Common rules. Input values are converted to double; every scaling multiplies (or divides) the real and imaginary parts separately by a double. `maxLevel` = the last (largest) decode level. Side data are float32: a side value is computed in double and rounded to float once; "divide side value by `f`" means `side = float(double(side) / f)`. Maxima skip non-finite candidates. `RMS(set) = sqrt(Σ(re² + im²) / (2·count))` over values with both parts finite; with `count = 0` it is NaN (0/0), as in Dysco. `LC(v) = max(max(re, im), −min(re, im))` with C++ `std::max`/`std::min` argument order (a NaN in the first argument propagates). Loops run in the order written. Rows are baselines `b` in chunk order; `vis = f·P + p`.

**Row.** For each row `b`: `m` = max over its values of `max(|re|, |im|)` (finite only, start 0); multiply the row by `maxLevel/m` (1 if `m = 0`); `side[b] = float(m / maxLevel)`.
Decode: `x = float(double(L[s]) · double(side[b]))`.

**RF.** (1) For each row `b`, each pol `p`: `m` = max of `max(|re|, |im|)` over channels (finite only); multiply by `maxLevel/m` (1 if `m = 0`); `side[F·P + b·P + p] = float(m / maxLevel)` (1 if `maxLevel = 0`). (2) For each `vis`: `m` = max over rows of the same quantity; `f = maxLevel/m` (1 if `m = 0`); `side[vis] = float(1/f)`; multiply that column by `f`.
Decode: `x = float(double(L[s]) · (double(side[vis]) · double(side[F·P + b·P + p])))`.

**AF** (with DyscoStMan's `fitToMaximum`). Antenna indices come from the antenna map; `A` = `n_antenna`.
1. For each `vis`: `r` = RMS over rows; divide the column by `r` if `r ≠ 0`; `side[vis] = float(r)`.
2. For each pol `p`: antenna RMS `g[0..A)` by iteration over the symmetric matrix `M[a1][a2]` = RMS of pol `p` over the row(s) of cross-correlation `(min, max)` (autocorrelations excluded; NaN when absent). Start `g = 1`. Up to 100 iterations while `precision > 1e-6` (initially 1): `next[i] = Σ_j M[i][j] / Σ_j g[j]` over `j ≠ i` with `g[j] ≠ 0` and `M[i][j]` finite (0 if the weight sum is 0); `g[i] = next[i]·0.8 + g[i]·0.2`; `mx` = max `g`; zero every `g[i] < mx·1e-5`; `precision = max |g[i] − next[i]| / mx`. Then multiply pol `p` of each row by `1/(g[a1]·g[a2])` (0 if the product is 0) and set `side[F·P + p·A + a] = float(g[a])`.
3. For each `vis`: `m` = max `LC` over cross-correlation rows (finite only); scale the column by `f = maxLevel/m` (1 if `m = 0` or `maxLevel = 0`) and divide `side[vis]` by `f`.
4. For each pol `p`, repeat: find the channel with the largest gain `Σ |re·k| + |im·k|` over cross rows (finite terms only), `k = maxLevel/m_ch − 1` (0 if `m_ch = 0`), strictly greater than the previous best (start 0); likewise per antenna with `m_a` = max `LC` over cross rows containing it and `k_a = maxLevel/m_a − 1`, each row contributing to both of its antennas. If the best antenna gain is strictly greater than the best channel gain: `f = maxLevel/m_a` (1 if `m_a = 0`); stop if `f < 1`, else divide `side[F·P + p·A + a]` by `f`, multiply pol `p` of every row by `f` once per occurrence of `a` in it (twice for its autocorrelation), and continue while `f > 1.01`. Otherwise with `f = k + 1` of the best channel (1 if none): stop if `f < 1`, else apply it as in step 3 to `vis = ch·P + p` and continue while `f > 1.001`.

Autocorrelations are scaled like any other row and in practice clip at the table ends: use RF or Row when autocorrelations matter.
Decode: `x = float(double(L[s]) · (double(side[vis]) · (double(g'[a1]) · double(g'[a2]))))` with `g'` the stored antenna factors of pol `p`.

In all cases the symbol of a non-finite input decodes to NaN (`L[Q−1] = NaN`).

## 9. Weights (draft)
Per block: `w[b][f] = min over p` of the input; `max` = the largest `w`, or 1 if it is 0; symbol = round-half-away-from-zero of `float(double(w) × (double(Q−1) / max))`; side data = `max`. Decoded value `= float(symbol × (double(max) / double(Q−1)))`, replicated over polarizations. Inputs MUST be finite and ≥ 0.
