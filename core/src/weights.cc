// Port of Dysco's WeightBlockEncoder (weightblockencoder.h @ aa30453).
// SPDX-License-Identifier: GPL-3.0-only
#include "weights.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace dp3core::dysco::detail {
namespace {
float MinOverPols(const float* w, std::size_t n_pol) {
  float weight = w[0];
  for (std::size_t p = 1; p != n_pol; ++p)
    if (w[p] < weight) weight = w[p];
  return weight;
}
}  // namespace

void EncodeWeightBlock(const float* weights, std::size_t n_baseline,
                       std::size_t n_channel, std::size_t n_pol,
                       std::size_t quant_count, float* side,
                       std::uint32_t* symbols) {
  const std::size_t n = n_baseline * n_channel * n_pol;
  for (std::size_t i = 0; i != n; ++i) {
    if (!std::isfinite(weights[i]) || weights[i] < 0.0f)
      throw std::invalid_argument("dysco weights: value " + std::to_string(weights[i]) +
                                  " at index " + std::to_string(i) +
                                  " is negative or not finite");
  }
  float max_value = 0.0f;
  for (std::size_t bc = 0; bc != n_baseline * n_channel; ++bc) {
    const float weight = MinOverPols(weights + bc * n_pol, n_pol);
    if (weight > max_value) max_value = weight;
  }
  if (max_value == 0.0f) max_value = 1.0f;
  *side = max_value;
  const double scale = double(quant_count - 1) / double(max_value);
  for (std::size_t bc = 0; bc != n_baseline * n_channel; ++bc) {
    const float weight = MinOverPols(weights + bc * n_pol, n_pol);
    // roundf of the product converted to float, as in Dysco.
    symbols[bc] = static_cast<std::uint32_t>(
        std::round(static_cast<float>(double(weight) * scale)));
  }
}

void DecodeWeightBlock(const std::uint32_t* symbols, std::size_t n_baseline,
                       std::size_t n_channel, std::size_t n_pol,
                       std::size_t quant_count, float side, float* weights) {
  const double scale = double(side) / double(quant_count - 1);
  for (std::size_t bc = 0; bc != n_baseline * n_channel; ++bc) {
    const float value = static_cast<float>(double(symbols[bc]) * scale);
    for (std::size_t p = 0; p != n_pol; ++p) weights[bc * n_pol + p] = value;
  }
}

}  // namespace dp3core::dysco::detail
