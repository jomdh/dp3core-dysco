// Linear weight quantizer, ported from Dysco's weightblockencoder.h
// (spec/SPEC.md section 9). Internal header: not installed.
// SPDX-License-Identifier: GPL-3.0-only
#ifndef DP3CORE_DYSCO_SRC_WEIGHTS_H_
#define DP3CORE_DYSCO_SRC_WEIGHTS_H_

#include <cstddef>
#include <cstdint>

namespace dp3core::dysco::detail {

/// Encodes one block of weights, (baseline, channel, pol) order, into
/// n_baseline * n_channel symbols in [0, quant_count) and one side float.
/// @throws std::invalid_argument if a weight is negative or not finite.
void EncodeWeightBlock(const float* weights, std::size_t n_baseline,
                       std::size_t n_channel, std::size_t n_pol,
                       std::size_t quant_count, float* side,
                       std::uint32_t* symbols);

/// Decodes one block; every polarization gets the channel's value.
void DecodeWeightBlock(const std::uint32_t* symbols, std::size_t n_baseline,
                       std::size_t n_channel, std::size_t n_pol,
                       std::size_t quant_count, float side, float* weights);

}  // namespace dp3core::dysco::detail

#endif  // DP3CORE_DYSCO_SRC_WEIGHTS_H_
