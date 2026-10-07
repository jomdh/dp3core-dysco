# dp3core-dysco-zarr

Dysco lossy compression for radio-interferometric visibilities and weights (Offringa 2016, A&A 595, A99), as a Zarr codec.

## Highlights

- Encodes visibility and weight chunks with the Dysco method.
- Zarr v3 codecs (`dp3core.dysco`, `dp3core.dysco-weights`) and Zarr v2 codecs.
- Registers both codecs with TensorStore's zarr3 driver, pinned to v0.1.85.
- C++17 core with no casacore or DP3 dependency.

Format: [`spec/SPEC.md`](spec/SPEC.md).

## Usage

```python
import zarr
from dp3core_dysco.zarr import DyscoCodec, DyscoWeightsCodec

zarr.create_array(
    "vis.zarr",
    shape=(899, 703, 64, 4),
    chunks=(10, 703, 64, 4),
    dtype="complex64",
    serializer=DyscoCodec(
        bits=10,
        normalization="af",
        antenna1=a1,
        antenna2=a2,
        n_antennas=38,
    ),
    compressors=[zarr.codecs.Crc32cCodec()],
)
```

Axis order is time, baseline, frequency, polarization. Put a `transpose` codec in front for other layouts.

Zarr v2 uses `dp3core_dysco.numcodecs.Dysco(chunk_shape=..., ...)` as the compressor, with no filters.

## Installation

C++17. Python install needs pybind11 and scikit-build-core.

```sh
pip install --no-build-isolation .
```

## Development

CMake >= 3.20. CMake >= 3.24 and nasm for the TensorStore layer.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDP3CORE_DYSCO_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`scripts/check.sh` builds and tests every available compiler configuration. `scripts/check-python.sh [venv]` builds into a venv and runs the Python tests.

GSL is used by the quantization-table comparison. Point `-DDP3CORE_DYSCO_REFERENCE_DIR` at a checkout of `aroffringa/dysco` (or set that variable for `scripts/check.sh`) to build `test_reference_dysco`, which checks side data, symbols, and decoded values against Dysco's casacore-free encoders.

TensorStore:

```sh
cmake -S . -B build-ts -G Ninja -DCMAKE_BUILD_TYPE=Release -DDP3CORE_DYSCO_TENSORSTORE=ON
cmake --build build-ts --target test_tensorstore_codec && build-ts/tensorstore/test_tensorstore_codec
```

TensorStore v0.1.85 and its dependencies are fetched. Override the archive with `-DDP3CORE_DYSCO_TENSORSTORE_URL=file:///...`. Link `dp3core_dysco::tensorstore` whole-archive so the codec registration stays. Registration uses TensorStore internal headers (`tensorstore/driver/zarr3/codec/`); recheck it on a version bump. `scripts/check-tensorstore.sh [build-dir] [venv]` runs the interop test.

Layout:

- `core/`: arrays in, bytes out. Chunk API in `dp3core/dysco/codec.h`.
- `python/`: Zarr v3 (`zarr.codecs`) and Zarr v2 (`numcodecs.codecs`).
- `tensorstore/`: codec registration for TensorStore's zarr3 driver.
- The DP3Core `io::Codec` adapter lives in DP3Core.

## Licence

GPL-3.0-only. See `LICENSE` and `PROVENANCE.md`. The vendored xxHash header in `third_party/xxhash/` is BSD-2-Clause.
