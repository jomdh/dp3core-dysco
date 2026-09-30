// LSB-first bit packing of fixed-width symbols (spec/SPEC.md section 6).
// SPDX-License-Identifier: GPL-3.0-only
#ifndef DP3CORE_DYSCO_BITPACK_H_
#define DP3CORE_DYSCO_BITPACK_H_

#include <cstddef>
#include <cstdint>

namespace dp3core::dysco {

/// Bytes needed for @p count symbols of @p bits bits: ceil(count * bits / 8).
constexpr std::size_t PackedBytes(std::size_t count, unsigned bits) {
  return (count * bits + 7) / 8;
}

/// Packs @p count symbols (each < 2^bits, 1 <= bits <= 32) into
/// PackedBytes(count, bits) bytes. Symbol j occupies stream bits
/// [j*bits, (j+1)*bits); stream bit q is bit (q mod 8) of byte q/8. Unused high
/// bits of the last byte are zero. Higher symbol bits are ignored.
void PackSymbols(const std::uint32_t* symbols, std::size_t count, unsigned bits,
                 std::byte* out);

/// Inverse of PackSymbols.
void UnpackSymbols(const std::byte* in, std::size_t count, unsigned bits,
                   std::uint32_t* symbols);

}  // namespace dp3core::dysco

#endif
