# dp3core-dysco-zarr

A standalone port of the **Dysco** lossy compression method for radio-interferometric
visibilities and weights (Offringa 2016, A&A 595, A99), packaged as a Zarr codec.

> **Convention notice.** The codec names `dp3core.dysco` and `dp3core.dysco-weights`,
> every configuration field, the chunk byte layout and any parset keys used with this
> library are **unofficial DP3Core conventions**. They are not endorsed by LOFAR, ASTRON
> or the MSv4 schema group.

Status: **pre-release, local development only.** Nothing here is published.

## Layers (planned)
1. `core/`: C++17 library, arrays in, bytes out. No casacore, no DP3 dependency.
2. `python/`: Zarr v3 array-to-bytes codecs (`zarr.codecs` entry point) and Zarr v2
   codecs (`numcodecs.codecs` entry point). *Not started.*
3. `tensorstore/`: TensorStore integration (needs a pinned TensorStore). *Not started.*
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

## Licence
GPL-3.0-only (see `LICENSE`). This is a derivative work of Dysco, which is published
under GPL version 3. See `PROVENANCE.md`. The vendored xxHash header in
`third_party/xxhash/` is BSD-2-Clause.
