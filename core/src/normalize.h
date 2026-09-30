// Per-block normalizations, ported from Dysco (spec/SPEC.md section 8).
// Internal header: not installed.
// SPDX-License-Identifier: GPL-3.0-only
#ifndef DP3CORE_DYSCO_SRC_NORMALIZE_H_
#define DP3CORE_DYSCO_SRC_NORMALIZE_H_

#include <complex>
#include <cstddef>
#include <cstdint>

namespace dp3core::dysco::detail {

/// One block (time slot): n_baseline rows of n_channel x n_pol values,
/// in (baseline, channel, pol) order.
struct BlockLayout {
  std::size_t n_baseline = 0;
  std::size_t n_channel = 0;
  std::size_t n_pol = 0;
  std::size_t n_antenna = 0;          // AF only
  const std::uint32_t* antenna1 = nullptr;  // AF only, per baseline
  const std::uint32_t* antenna2 = nullptr;  // AF only, per baseline
};

/// Normalizes @p data in place (Dysco's encoders, in their loop order and
/// with their operations) and writes the side data: Row n_baseline floats;
/// RF n_channel*n_pol channel then n_baseline*n_pol row floats; AF
/// n_channel*n_pol channel then n_pol*n_antenna antenna floats.
/// @p max_level is the largest decode level.
void NormalizeRow(std::complex<double>* data, const BlockLayout& layout,
                  double max_level, float* side);
void NormalizeRF(std::complex<double>* data, const BlockLayout& layout,
                 double max_level, float* side);
/// AF with fitToMaximum (as DyscoStMan always uses it).
void NormalizeAF(std::complex<double>* data, const BlockLayout& layout,
                 double max_level, float* side);

/// Decodes a block of symbols (2 per value, real first). @p levels has
/// n_levels + 1 entries, the last being NaN (the symbol of non-finite input).
void DenormalizeRow(const std::uint32_t* symbols, const float* levels,
                    const BlockLayout& layout, const float* side,
                    std::complex<float>* out);
void DenormalizeRF(const std::uint32_t* symbols, const float* levels,
                   const BlockLayout& layout, const float* side,
                   std::complex<float>* out);
void DenormalizeAF(const std::uint32_t* symbols, const float* levels,
                   const BlockLayout& layout, const float* side,
                   std::complex<float>* out);

}  // namespace dp3core::dysco::detail

#endif  // DP3CORE_DYSCO_SRC_NORMALIZE_H_
