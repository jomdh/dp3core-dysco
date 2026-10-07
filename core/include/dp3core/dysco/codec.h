// Chunk-level encode/decode API (design section 7.1, spec/SPEC.md).
// SPDX-License-Identifier: GPL-3.0-only
//
// Convention notice: unofficial DP3Core conventions, not endorsed by LOFAR,
// ASTRON or the MSv4 schema group.
#ifndef DP3CORE_DYSCO_CODEC_H_
#define DP3CORE_DYSCO_CODEC_H_

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dp3core/dysco/header.h"
#include "dp3core/dysco/types.h"

namespace dp3core::dysco {

/// Baseline-to-antenna map, required for AF. One entry per baseline; every
/// index must be < n_antenna.
struct AntennaMap {
  std::uint32_t n_antenna = 0;
  std::vector<std::uint32_t> antenna1;
  std::vector<std::uint32_t> antenna2;
};

struct DataParams {
  unsigned bits = 10;
  Distribution distribution = Distribution::kTruncatedGaussian;
  double truncation = 2.5;
  Normalization normalization = Normalization::kAF;
  std::uint64_t seed = 0;  ///< dither seed, supplied by the caller
  bool embed_dictionary = false;
  bool embed_antenna_map = false;  ///< AF only
};

struct WeightParams {
  unsigned bits = 12;
};

/// Encoding output does not depend on these options.
struct EncodeOptions {
  unsigned threads = 1;  ///< worker threads, split across blocks (time slots)
};

/// Checks parameters, shape and (for AF) the antenna map.
/// @throws std::invalid_argument with a precise message.
void Validate(const DataParams& params, const Shape& shape, const AntennaMap* map);
void Validate(const WeightParams& params, const Shape& shape);

/// Exact encoded size in bytes; depends only on parameters and shape (and
/// n_antenna for AF). @throws std::invalid_argument.
std::size_t EncodedSize(const DataParams& params, const Shape& shape,
                        const AntennaMap* map);
std::size_t EncodedSize(const WeightParams& params, const Shape& shape);

/// Encodes a C-order (time, baseline, channel, pol) complex64 chunk. Writes
/// exactly EncodedSize() bytes. Non-finite values are stored as "NaN".
/// @throws std::invalid_argument on bad parameters or a too small buffer.
void EncodeData(const DataParams& params, const Shape& shape, const AntennaMap* map,
                const std::complex<float>* in, std::byte* out, std::size_t out_size,
                const EncodeOptions& options = {});

/// Same encode, with one extra rule. On the outermost dictionary interval,
/// when the denormalized distance between the two centroids is greater than
/// 20, the nearer centroid is stored instead of the dithered one. Interior
/// intervals stay dithered. The default overload does not apply this rule.
void EncodeData(const DataParams& params, const Shape& shape, const AntennaMap* map,
                const std::complex<float>* in, std::byte* out, std::size_t out_size,
                const EncodeOptions& options, bool nearest_wide_outer_bin);

/// Decodes a chunk that must hold exactly @p shape. @p map is needed for AF
/// unless the chunk embeds it; if both are present they must be equal.
/// @throws FormatError for invalid or mismatching bytes,
///         std::invalid_argument for a missing or invalid map.
void DecodeData(const std::byte* in, std::size_t in_size, const Shape& shape,
                const AntennaMap* map, std::complex<float>* out);

/// Weights: finite and >= 0, (time, baseline, channel, pol) order.
void EncodeWeights(const WeightParams& params, const Shape& shape, const float* in,
                   std::byte* out, std::size_t out_size,
                   const EncodeOptions& options = {});
void DecodeWeights(const std::byte* in, std::size_t in_size, const Shape& shape,
                   float* out);

}  // namespace dp3core::dysco

#endif  // DP3CORE_DYSCO_CODEC_H_
