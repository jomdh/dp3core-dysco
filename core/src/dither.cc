// SPDX-License-Identifier: GPL-3.0-only
#include "dp3core/dysco/dither.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

namespace dp3core::dysco {
namespace {
bool HostIsLittleEndian() {
  const std::uint32_t one = 1;
  unsigned char first;
  std::memcpy(&first, &one, 1);
  return first == 1;
}
}  // namespace

std::uint64_t Xxh3(const void* data, std::size_t size) {
  return XXH3_64bits(data, size);
}

std::uint64_t ChunkNonce(const std::complex<float>* values, std::size_t count) {
  static_assert(sizeof(std::complex<float>) == 8, "complex<float> layout");
  if (HostIsLittleEndian()) return XXH3_64bits(values, count * 8);
  std::vector<unsigned char> bytes(count * 8);
  for (std::size_t i = 0; i != count; ++i) {
    const float parts[2] = {values[i].real(), values[i].imag()};
    for (std::size_t c = 0; c != 2; ++c) {
      std::uint32_t u;
      std::memcpy(&u, &parts[c], 4);
      for (std::size_t k = 0; k != 4; ++k)
        bytes[i * 8 + c * 4 + k] = static_cast<unsigned char>(u >> (8 * k));
    }
  }
  return XXH3_64bits(bytes.data(), bytes.size());
}

std::uint32_t QuantizeDithered(float value, const float* levels,
                               std::size_t n_levels, std::uint32_t dither) {
  if (!std::isfinite(value)) return static_cast<std::uint32_t>(n_levels);
  const float* end = levels + n_levels;
  const float* right = std::lower_bound(levels, end, value);
  if (right == levels) return 0;
  if (right == end) return static_cast<std::uint32_t>(n_levels - 1);
  const float hi = *right;
  const float lo = *(right - 1);
  // Same float expression and evaluation order as Dysco.
  const float mark = float(1u << 31) * (value - lo) / (hi - lo);
  const std::size_t j = static_cast<std::size_t>(right - levels);
  return static_cast<std::uint32_t>(mark > static_cast<float>(dither) ? j
                                                                      : j - 1);
}

}  // namespace dp3core::dysco
