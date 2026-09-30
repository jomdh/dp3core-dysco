// Portable quantization tables (spec/SPEC.md section 4).
// SPDX-License-Identifier: GPL-3.0-only
//
// Convention notice: unofficial DP3Core conventions, not endorsed by LOFAR,
// ASTRON or the MSv4 schema group.
#ifndef DP3CORE_DYSCO_TABLES_H_
#define DP3CORE_DYSCO_TABLES_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "dp3core/dysco/types.h"

namespace dp3core::dysco {

/// The decode table ("dictionary") for data: the 2^bits - 1 quantization levels
/// of a unit-RMS distribution, strictly increasing, exactly symmetric around the
/// middle entry, which is 0. Symbol 2^bits - 1 is reserved for non-finite
/// values and is not in the table.
///
/// The values depend only on the arguments: they are computed with IEEE-754
/// basic operations only, so they are identical on every platform.
///
/// @param truncation Truncation in sigma; used only for kTruncatedGaussian,
///        where it must be in (0, kMaxTruncation]. Ignored otherwise.
/// @throws std::invalid_argument for an unsupported bit count or truncation.
std::vector<float> DecodeTable(unsigned bits, Distribution distribution,
                               double truncation);

/// XXH3-64 (seed 0) of the table serialized as float32 little-endian.
/// This is the header's dict_checksum.
std::uint64_t TableChecksum(const std::vector<float>& table);

/// A table and its checksum, built once per (bits, distribution, truncation)
/// and shared (thread-safe; the 16-bit table takes about a second to build).
struct CachedTable {
  std::vector<float> levels;  ///< DecodeTable(...)
  std::uint64_t checksum;     ///< TableChecksum(levels)
};
/// @throws std::invalid_argument like DecodeTable.
std::shared_ptr<const CachedTable> GetCachedTable(unsigned bits,
                                                  Distribution distribution,
                                                  double truncation);

/// Low-level functions, public for tests and the equivalence harness.
namespace portable {

/// exp(x) for x <= 0, computed with basic operations only.
double Exp(double x);

/// erfc(z) for z >= 0, computed with basic operations only.
double Erfc(double z);

/// The standard normal CDF for x <= 0 (lower tail), accurate in relative terms.
double PhiLower(double x);

/// Inverse of PhiLower for 0 < p <= 0.5: 64 bisection steps on [-16, 0].
double InversePhiLower(double p);

}  // namespace portable

}  // namespace dp3core::dysco

#endif
