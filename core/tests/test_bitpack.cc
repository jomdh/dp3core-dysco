// SPDX-License-Identifier: GPL-3.0-only
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

TEST_CASE(HandVectors) {
  // 10 bits: 0x3FF, 0x001 -> bits 0..9 all set, bit 10 set.
  {
    const std::uint32_t s[] = {0x3FF, 0x001};
    std::byte out[3] = {};
    PackSymbols(s, 2, 10, out);
    CHECK_EQ(std::to_integer<unsigned>(out[0]), 0xFFu);
    CHECK_EQ(std::to_integer<unsigned>(out[1]), 0x07u);  // bits 8,9 (sym0), bit 10 (sym1)
    CHECK_EQ(std::to_integer<unsigned>(out[2]), 0x00u);
  }
  // 3 bits: 1,2,3,4,5 -> 0b 101 100 011 010 001 = 0x0B11 >> ... check bytes.
  {
    const std::uint32_t s[] = {1, 2, 3, 4, 5};
    std::byte out[2] = {};
    PackSymbols(s, 5, 3, out);
    // stream: 001 010 011 100 101 (LSB first) -> value = 1 | 2<<3 | 3<<6 | 4<<9 | 5<<12
    const unsigned v = 1 | 2 << 3 | 3 << 6 | 4 << 9 | 5 << 12;
    CHECK_EQ(std::to_integer<unsigned>(out[0]), v & 0xFF);
    CHECK_EQ(std::to_integer<unsigned>(out[1]), (v >> 8) & 0xFF);
  }
  // 16 bits is little-endian u16.
  {
    const std::uint32_t s[] = {0xBEEF};
    std::byte out[2] = {};
    PackSymbols(s, 1, 16, out);
    CHECK_EQ(std::to_integer<unsigned>(out[0]), 0xEFu);
    CHECK_EQ(std::to_integer<unsigned>(out[1]), 0xBEu);
  }
  CHECK_EQ(PackedBytes(0, 10), 0u);
  CHECK_EQ(PackedBytes(1, 3), 1u);
  CHECK_EQ(PackedBytes(3, 3), 2u);
  CHECK_EQ(PackedBytes(4, 10), 5u);
}

TEST_CASE(RoundTripAllWidthsAndLengths) {
  std::uint64_t state = 7;
  for (unsigned bits = 1; bits <= 16; ++bits) {
    for (std::size_t count = 0; count <= 67; ++count) {
      std::vector<std::uint32_t> in(count), back(count, 0xFFFFFFFFu);
      for (auto& s : in) s = static_cast<std::uint32_t>(Mix(++state) & ((1u << bits) - 1));
      std::vector<std::byte> packed(PackedBytes(count, bits) + 1, std::byte{0xCC});
      PackSymbols(in.data(), count, bits, packed.data());
      // No write past the end; unused high bits zero.
      CHECK(packed.back() == std::byte{0xCC});
      if (count * bits % 8 != 0) {
        const unsigned used = static_cast<unsigned>(count * bits % 8);
        CHECK((std::to_integer<unsigned>(packed[packed.size() - 2]) >> used) == 0);
      }
      UnpackSymbols(packed.data(), count, bits, back.data());
      CHECK(in == back);
    }
  }
}

TEST_CASE(HighBitsIgnored) {
  const std::uint32_t s[] = {0xFFFFFFF5u};
  std::byte out[1] = {};
  PackSymbols(s, 1, 4, out);
  CHECK_EQ(std::to_integer<unsigned>(out[0]), 0x05u);
}
