// Compares this port with the original Dysco sources, compiled from a local
// checkout of aroffringa/dysco (CMake: -DDP3CORE_DYSCO_REFERENCE_DIR=<path>).
// Only Dysco's casacore-free parts are used. GPL-3.0, like this library.
// SPDX-License-Identifier: GPL-3.0-only
#include <bytepacker.h>

#include <cmath>

#include <cstdint>
#include <vector>

#include "check.h"
#include "dp3core/dysco/bitpack.h"

using namespace dp3core::dysco;

namespace {
std::uint64_t Mix(std::uint64_t z) {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
}  // namespace

TEST_CASE(PackingEqualsDyscoBytePacker) {
  const unsigned widths[] = {2, 3, 4, 6, 8, 10, 12, 16};
  std::uint64_t state = 99;
  for (unsigned bits : widths) {
    for (std::size_t count = 0; count <= 40; ++count) {
      std::vector<unsigned> in(count);
      std::vector<std::uint32_t> in32(count);
      for (std::size_t i = 0; i != count; ++i)
        in32[i] = in[i] = static_cast<unsigned>(Mix(++state) & ((1u << bits) - 1));
      const std::size_t n = dyscostman::BytePacker::bufferSize(count, bits);
      CHECK_EQ(n, PackedBytes(count, bits));
      std::vector<unsigned char> theirs(n + 8, 0);
      std::vector<std::byte> ours(n + 8, std::byte{0});
      dyscostman::BytePacker::pack(bits, theirs.data(), in.data(), count);
      PackSymbols(in32.data(), count, bits, ours.data());
      bool same = true;
      for (std::size_t i = 0; i != n; ++i)
        same = same && std::to_integer<unsigned>(ours[i]) == theirs[i];
      if (!same) {
        std::cerr << "bits " << bits << " count " << count << '\n';
        CHECK(same);
      }
    }
  }
}

#include <stochasticencoder.h>

#include "dp3core/dysco/dither.h"

TEST_CASE(QuantizerEqualsDyscoEncodeWithDithering) {
  // Dysco's own table (GSL) and quantizer versus QuantizeDithered on the same
  // table: the quantizer logic must agree for every input and dither.
  const std::size_t quant_counts[] = {4, 16, 256, 4096};
  std::uint64_t state = 5;
  for (std::size_t q : quant_counts) {
    const auto encoder =
        dyscostman::StochasticEncoder<float>::TruncatedGausEncoder(q, 2.5, 1.0);
    std::vector<float> levels(q - 1);
    for (std::size_t s = 0; s != q - 1; ++s)
      levels[s] = encoder.Decode(static_cast<unsigned>(s));
    std::size_t mismatches = 0;
    for (int i = 0; i != 200000; ++i) {
      const std::uint64_t r = Mix64(++state);
      float v;
      if (i % 50 == 0) {
        v = levels[r % levels.size()];  // exactly on a level
      } else {
        v = static_cast<float>((double(r >> 11) / double(1ull << 53) - 0.5) * 7.0);
      }
      if (i % 997 == 0) v = std::nanf("");
      const std::uint32_t dither = static_cast<std::uint32_t>(Mix64(r) >> 33);
      const unsigned theirs = encoder.EncodeWithDithering(v, dither);
      const std::uint32_t ours = QuantizeDithered(v, levels.data(), levels.size(), dither);
      if (theirs != ours) ++mismatches;
    }
    CHECK_EQ(mismatches, std::size_t{0});
  }
}
