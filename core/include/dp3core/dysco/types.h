// Basic codec types.
// SPDX-License-Identifier: GPL-3.0-only
//
// Convention notice: the codec names, configuration fields and chunk layout of
// this library are unofficial DP3Core conventions, not endorsed by LOFAR, ASTRON
// or the MSv4 schema group.
#ifndef DP3CORE_DYSCO_TYPES_H_
#define DP3CORE_DYSCO_TYPES_H_

#include <cstddef>
#include <cstdint>

namespace dp3core::dysco {

/// Quantization distribution. The numeric values are the header codes.
enum class Distribution : std::uint8_t {
  kGaussian = 0,
  kTruncatedGaussian = 1,
  kUniform = 2,
};

/// Normalization method. The numeric values are the header codes.
enum class Normalization : std::uint8_t {
  kAF = 0,   ///< antenna-factor (needs the baseline-to-antenna map)
  kRF = 1,   ///< row-factor (per row and pol, then per channel and pol)
  kRow = 2,  ///< one factor per row
};

/// What a chunk holds. The numeric values are the header codes.
enum class Kind : std::uint8_t {
  kData = 1,     ///< complex64 visibilities
  kWeights = 2,  ///< float32 weights
};

/// Header code for "not applicable" (distribution/normalization of weights).
inline constexpr std::uint8_t kNotApplicable = 255;

/// Chunk shape in codec axis order: (time, baseline, frequency, polarization),
/// C order (polarization fastest).
struct Shape {
  std::size_t n_time = 0;
  std::size_t n_baseline = 0;
  std::size_t n_channel = 0;
  std::size_t n_pol = 0;
};

/// True for the bit counts Dysco supports: 2, 3, 4, 6, 8, 10, 12, 16.
constexpr bool IsSupportedBitCount(unsigned bits) {
  return bits == 2 || bits == 3 || bits == 4 || bits == 6 || bits == 8 ||
         bits == 10 || bits == 12 || bits == 16;
}

/// Largest accepted truncation (in sigma) for the truncated Gaussian.
inline constexpr double kMaxTruncation = 8.0;

const char* ToString(Distribution);
const char* ToString(Normalization);
const char* ToString(Kind);

}  // namespace dp3core::dysco

#endif
