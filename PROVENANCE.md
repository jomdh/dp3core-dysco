# Provenance

This library is a **port** (a derivative work) of Dysco:

- Dysco, https://github.com/aroffringa/dysco , read at commit
  `aa304537009e949f7079af4a3ce0fc28f1374bbf` (2026-04-24). Author: André Offringa,
  with contributions by Marcel Loose, José Sabater Montes and Tammo Jan Dijkema.
  Published under "GPL version 3" (`package.dox`, `uvector.h`, `LICENSE`).
- The same code as shipped in casacore (`tables/Dysco`, commit `de676b9`) was used
  to cross-check behaviour.
- Method: A. R. Offringa, "Compression of interferometric radio-astronomical data",
  A&A 595, A99 (2016), arXiv:1609.02019. Please cite it when using this library.

Upstream does not grant "or any later version", so this port is licensed
**GPL-3.0-only**.

What is ported (algorithmic behaviour): the quantization dictionaries (Gaussian,
truncated Gaussian, uniform), dithered quantization, the AF / RF / Row
normalizations, the weight quantizer and the per-block side data.

What is deliberately different (see `spec/SPEC.md`):
- The random dither comes from a counter-based generator (`splitmix64-v1`) keyed by a
  seed plus a hash of the chunk content, so encoding is reproducible. The original
  seeds `std::mt19937` from `std::random_device`.
- The quantization tables are computed with a portable, libm-free algorithm, so they
  are identical on every platform. The original uses GSL and `long double`.
- The chunk byte layout (versioned little-endian header, explicit byte order) is new.
  It is not compatible with casacore DyscoStMan files.

Third-party code: `third_party/xxhash/xxhash.h`, xxHash 0.8.3 by Yann Collet,
BSD-2-Clause (`third_party/xxhash/LICENSE`).
