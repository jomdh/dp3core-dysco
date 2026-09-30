// Counter-based dither and dithered quantization (spec/SPEC.md section 7).
// SPDX-License-Identifier: GPL-3.0-only
#ifndef DP3CORE_DYSCO_DITHER_H_
#define DP3CORE_DYSCO_DITHER_H_

#include <complex>
#include <cstddef>
#include <cstdint>

namespace dp3core::dysco {

/// The SplitMix64 output function: returns the output of a SplitMix64
/// generator whose state was @p z before the step ("splitmix64-v1").
constexpr std::uint64_t Mix64(std::uint64_t z) {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

/// Per-chunk dither key: mix(seed ^ mix(nonce)).
constexpr std::uint64_t DitherKey(std::uint64_t seed, std::uint64_t nonce) {
  return Mix64(seed ^ Mix64(nonce));
}

/// The dither value in [0, 2^31) for element counter @p i.
constexpr std::uint32_t DitherValue(std::uint64_t key, std::uint64_t i) {
  return static_cast<std::uint32_t>(Mix64(key + i) >> 33);
}

/// Element counter of the real (c = 0) or imaginary (c = 1) part of
/// (t, b, f, p) in a chunk with B baselines, F channels, P polarizations.
constexpr std::uint64_t DitherCounter(std::uint64_t t, std::uint64_t b,
                                      std::uint64_t f, std::uint64_t p,
                                      std::uint64_t c, std::uint64_t n_baseline,
                                      std::uint64_t n_channel,
                                      std::uint64_t n_pol) {
  return (((t * n_baseline + b) * n_channel + f) * n_pol + p) * 2 + c;
}

/// XXH3-64 (seed 0) of the values as little-endian IEEE-754 bytes, on any
/// host byte order.
std::uint64_t ChunkNonce(const std::complex<float>* values, std::size_t count);

/// XXH3-64 (seed 0) of @p size bytes (exposed for tests and tools).
std::uint64_t Xxh3(const void* data, std::size_t size);

/// Dithered quantization of @p value against the ascending decode table
/// @p levels (n = @p n_levels entries), exactly as Dysco's
/// StochasticEncoder::EncodeWithDithering. Returns a symbol in [0, n]; n is
/// the symbol of non-finite values.
std::uint32_t QuantizeDithered(float value, const float* levels,
                               std::size_t n_levels, std::uint32_t dither);

}  // namespace dp3core::dysco

#endif  // DP3CORE_DYSCO_DITHER_H_
