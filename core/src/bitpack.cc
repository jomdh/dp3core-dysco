// SPDX-License-Identifier: GPL-3.0-only
#include "dp3core/dysco/bitpack.h"

namespace dp3core::dysco {

void PackSymbols(const std::uint32_t* symbols, std::size_t count, unsigned bits,
                 std::byte* out) {
  const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
  std::uint64_t accumulator = 0;
  unsigned filled = 0;
  std::size_t o = 0;
  for (std::size_t j = 0; j != count; ++j) {
    accumulator |= (symbols[j] & mask) << filled;
    filled += bits;
    while (filled >= 8) {
      out[o++] = static_cast<std::byte>(accumulator & 0xFFu);
      accumulator >>= 8;
      filled -= 8;
    }
  }
  if (filled > 0) out[o] = static_cast<std::byte>(accumulator & 0xFFu);
}

void UnpackSymbols(const std::byte* in, std::size_t count, unsigned bits,
                   std::uint32_t* symbols) {
  const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
  std::uint64_t accumulator = 0;
  unsigned available = 0;
  std::size_t i = 0;
  for (std::size_t j = 0; j != count; ++j) {
    while (available < bits) {
      accumulator |= static_cast<std::uint64_t>(std::to_integer<unsigned>(in[i++]))
                     << available;
      available += 8;
    }
    symbols[j] = static_cast<std::uint32_t>(accumulator & mask);
    accumulator >>= bits;
    available -= bits;
  }
}

}  // namespace dp3core::dysco
