# dp3core-dysco-zarr

A standalone port of the **Dysco** lossy compression method for radio-interferometric
visibilities and weights (Offringa 2016, A&A 595, A99), packaged as a Zarr codec.

> **Convention notice.** The codec names `dp3core.dysco` and `dp3core.dysco-weights`,
> every configuration field, the chunk byte layout and any parset keys used with this
> library are **unofficial DP3Core conventions**. They are not endorsed by LOFAR, ASTRON
> or the MSv4 schema group.

Status: **pre-release, local development only.** Nothing here is published.

# Zenodo 1044019 (clean input)

Run date: 2026-10-06.
DP3: DP3 6.6.0 Commit hash undefined.
Library version: 0.1.0.
Dataset: nrow=4809420 nchan=20 npol=4 nbl=1540 cross=1485 ntime=3123 regular=True flagged=0.055632830985856925.
Sigma median (cross, channel-difference MAD): 3.772195339202881. p10/p90: [2.978609323501587, 5.052494525909424].
Casacore encode s is the DP3 process wall time. Casacore decode s is the msextract read of that column. dp3core encode s and decode s are the codec calls. dp3core bytes are the sum of the encoded chunks, including each 80-byte header.
Errors are decoded minus the plain DP3 copy, on unflagged cross-correlations. DATA errors are divided by the shared per-baseline per-polarisation sigma. Both encoders use AF, truncated Gaussian 2.5, and seed 0 for dp3core. Weight bitrate on the casacore DATA runs is 12.

## DATA

| setting | DATA MB | ratio vs plain | bits/float | encode s | decode s | RMS/σ | p99.9/σ | max/σ | mean/σ | noise increase % |
|---|---|---|---|---|---|---|---|---|---|---|
| casacore plain | 3078.916133 | 1.000000 | 32.0092 | 11.992 | 4.289 | 0 | 1e-09 | 0 | 0 | 0 |
| casacore DyscoStMan 6-bit | 580.878085 | 5.300452 | 6.0390 | 19.377 | 4.324 | 0.058056774 | 0.39810717 | 6.348581 | -1.2528648e-06 | 0.16838768 |
| casacore DyscoStMan 8-bit | 773.254885 | 3.981761 | 8.0390 | 20.315 | 4.271 | 0.014885066 | 0.12589254 | 2.4867088 | 7.5254306e-07 | 0.011077646 |
| casacore DyscoStMan 10-bit | 965.631685 | 3.188499 | 10.0390 | 21.844 | 4.301 | 0.0038144617 | 0.033496544 | 0.68503239 | 3.4919705e-07 | 0.00072750325 |
| casacore DyscoStMan 12-bit | 1158.008485 | 2.658803 | 12.0390 | 23.369 | 4.202 | 0.00096193687 | 0.0084139514 | 0.16176891 | 4.3047218e-08 | 4.6266117e-05 |
| casacore DyscoStMan 16-bit | 1542.762085 | 1.995717 | 16.0390 | 27.040 | 4.427 | 6.0292039e-05 | 0.00053088444 | 0.011181459 | 2.8061985e-09 | 1.817565e-07 |
| dp3core.dysco 6-bit | 581.127840 | 5.298174 | 6.0416 | 65.609 | 0.806 | 0.058054717 | 0.39810717 | 8.5423815 | 9.0611644e-07 | 0.16837576 |
| dp3core.dysco 8-bit | 773.504640 | 3.980475 | 8.0416 | 72.420 | 0.861 | 0.014885238 | 0.12589254 | 1.8100827 | 8.5650476e-07 | 0.011077902 |
| dp3core.dysco 10-bit | 965.881440 | 3.187675 | 10.0416 | 79.229 | 0.935 | 0.0038152493 | 0.033496544 | 0.64970912 | 1.2744057e-07 | 0.00072780371 |
| dp3core.dysco 12-bit | 1158.258240 | 2.658229 | 12.0416 | 85.103 | 1.179 | 0.00096198035 | 0.0084139514 | 0.1945379 | -2.1281683e-08 | 4.6270299e-05 |
| dp3core.dysco 16-bit | 1543.011840 | 1.995394 | 16.0416 | 104.121 | 1.539 | 6.0296192e-05 | 0.00053088444 | 0.0093091538 | 2.8030408e-10 | 1.8178155e-07 |

## Weight rows

| setting | MB | ratio vs plain | bits/float | encode s | decode s | rms rel | max rel |
|---|---|---|---|---|---|---|---|
| casacore plain weights | 1539.458219 | 1.000000 | 32.0092 | 11.992 | 1.706 | 0 | 0 |
| casacore DyscoStMan 6-bit weights | 144.295187 | 10.668812 | 3.0003 | 19.377 | 1.396 | 0.057415703 | 0.77158173 |
| casacore DyscoStMan 8-bit weights | 144.295187 | 10.668812 | 3.0003 | 20.315 | 1.360 | 0.057415703 | 0.77158173 |
| casacore DyscoStMan 10-bit weights | 144.295187 | 10.668812 | 3.0003 | 21.844 | 1.639 | 0.057415703 | 0.77158173 |
| casacore DyscoStMan 12-bit weights | 144.295187 | 10.668812 | 3.0003 | 23.369 | 1.382 | 0.057415703 | 0.77158173 |
| casacore DyscoStMan 16-bit weights | 144.295187 | 10.668812 | 3.0003 | 27.040 | 1.341 | 0.057415703 | 0.77158173 |
| casacore dysco_d0_w12 | 144.295187 | 10.668812 | 3.0003 | 12.187 | 1.475 | 0.057415703 | 0.77158173 |
| dp3core.dysco weights 12-bit | 144.544932 | 10.650378 | 3.0055 | 0.547 | 0.192 | 0.057415703 | 0.77158173 |


## Layers (planned)
1. `core/`: C++17 library, arrays in, bytes out. No casacore, no DP3 dependency.
   Chunk API in `dp3core/dysco/codec.h` (`EncodeData`, `DecodeData`, `EncodeWeights`,
   `DecodeWeights`, `EncodedSize`, `Validate`). *Working; format still a draft.*
2. `python/`: Zarr v3 array-to-bytes codecs (`zarr.codecs` entry point) and Zarr v2
   codecs (`numcodecs.codecs` entry point). *Working (local builds only).*
3. `tensorstore/`: registers both codecs with TensorStore's zarr3 driver from our own
   code (no TensorStore patch), pinned to TensorStore v0.1.85. *Working (local builds only).*
4. DP3Core `io::Codec` adapter: lives in DP3Core, not here.

The format and algorithms are specified in [`spec/SPEC.md`](spec/SPEC.md).

## Build and test
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDP3CORE_DYSCO_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
or `scripts/check.sh`, which builds and tests every available compiler configuration.

Requirements: CMake >= 3.20 (>= 3.24 once the TensorStore layer is enabled), a C++17
compiler. Optional: GSL, used only by a test that compares our portable quantization
tables with tables built the way the original Dysco builds them.

Comparison with the original: configure with
`-DDP3CORE_DYSCO_REFERENCE_DIR=<checkout of aroffringa/dysco>` (or set that environment
variable for `scripts/check.sh`) to build `test_reference_dysco`, which compiles Dysco's
casacore-free encoders from that checkout (nothing is copied into this repository; GSL
required) and checks that side data, symbols and decoded values are bit-identical.

## Python
```sh
pip install --no-build-isolation .      # needs pybind11 and scikit-build-core
```
```python
import zarr
from dp3core_dysco.zarr import DyscoCodec, DyscoWeightsCodec

zarr.create_array("vis.zarr", shape=(899, 703, 64, 4), chunks=(10, 703, 64, 4),
                  dtype="complex64",
                  serializer=DyscoCodec(bits=10, normalization="af",
                                        antenna1=a1, antenna2=a2, n_antennas=38),
                  compressors=[zarr.codecs.Crc32cCodec()])
```
Zarr v2: `dp3core_dysco.numcodecs.Dysco(chunk_shape=..., ...)` as the `compressor`, no
filters. Only the canonical axis order (time, baseline, frequency, polarization) is
supported; use a `transpose` codec in front for other layouts.
`scripts/check-python.sh [venv]` builds into a venv and runs the Python tests.
`dp3core_dysco._reference` is a slow pure-NumPy decoder written from `spec/SPEC.md` only
(no compiled code); the tests use it to show the specification is sufficient. It is not a
user API.

## Licence
GPL-3.0-only (see `LICENSE`). This is a derivative work of Dysco, which is published
under GPL version 3. See `PROVENANCE.md`. The vendored xxHash header in
`third_party/xxhash/` is BSD-2-Clause.

## TensorStore
```sh
cmake -S . -B build-ts -G Ninja -DCMAKE_BUILD_TYPE=Release -DDP3CORE_DYSCO_TENSORSTORE=ON
cmake --build build-ts --target test_tensorstore_codec && build-ts/tensorstore/test_tensorstore_codec
```
CMake >= 3.24 and nasm are needed; TensorStore v0.1.85 and its dependencies are fetched
(override the archive with `-DDP3CORE_DYSCO_TENSORSTORE_URL=file:///...`). Link
`dp3core_dysco::tensorstore` into the binary or shared library that uses TensorStore; it
is linked whole-archive so the codec registration (a static initializer) is kept.
The registration uses TensorStore *internal* headers (`tensorstore/driver/zarr3/codec/`)
and must be rechecked on every TensorStore version bump.
`scripts/check-tensorstore.sh [build-dir] [venv]` also runs the zarr-python <-> TensorStore
interop test (byte-identical chunks, identical decode).
