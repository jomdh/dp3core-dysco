// SPDX-License-Identifier: GPL-3.0-only
#include "dp3core/dysco/dither.h"

#include <cmath>
#include <complex>
#include <cstring>
#include <limits>
#include <vector>

#include "check.h"

using namespace dp3core::dysco;

TEST_CASE(Mix64MatchesSplitMix64Reference) {
  // Outputs of Vigna's splitmix64.c seeded with 0.
  CHECK_EQ(Mix64(0), 0xE220A8397B1DCDAFull);
  CHECK_EQ(Mix64(0x9E3779B97F4A7C15ull), 0x6E789E6AA1B965F4ull);
  CHECK_EQ(Mix64(0x3C6EF372FE94F82Aull), 0x06C45D188009454Full);
}

TEST_CASE(DitherGoldenValues) {
  // Computed independently (Python, arbitrary precision integers).
  const std::uint64_t key = DitherKey(12345, 0xABCDEF);
  CHECK_EQ(key, 0xE1464B5CD3E71F98ull);
  CHECK_EQ(DitherValue(key, 0), 1007970733u);
  CHECK_EQ(DitherValue(key, 1), 1277869557u);
  CHECK_EQ(DitherValue(key, 2), 2116640778u);
  CHECK_EQ(DitherValue(key, 3), 1236081133u);
  static_assert(DitherKey(1, 2) == Mix64(1 ^ Mix64(2)), "constexpr");
}

TEST_CASE(DitherValuesAre31BitAndRoughlyUniform) {
  const std::uint64_t key = DitherKey(7, 8);
  double sum = 0;
  const int n = 200000;
  for (int i = 0; i != n; ++i) {
    const std::uint32_t u = DitherValue(key, static_cast<std::uint64_t>(i));
    CHECK(u < (1u << 31));
    sum += u;
  }
  const double mean = sum / n / double(1u << 31);
  CHECK(std::fabs(mean - 0.5) < 0.005);
}

TEST_CASE(CounterOrder) {
  CHECK_EQ(DitherCounter(0, 0, 0, 0, 0, 5, 6, 4), 0u);
  CHECK_EQ(DitherCounter(0, 0, 0, 0, 1, 5, 6, 4), 1u);
  CHECK_EQ(DitherCounter(0, 0, 0, 1, 0, 5, 6, 4), 2u);
  CHECK_EQ(DitherCounter(0, 0, 1, 0, 0, 5, 6, 4), 8u);
  CHECK_EQ(DitherCounter(0, 1, 0, 0, 0, 5, 6, 4), 48u);
  CHECK_EQ(DitherCounter(1, 0, 0, 0, 0, 5, 6, 4), 240u);
  CHECK_EQ(DitherCounter(2, 4, 5, 3, 1, 5, 6, 4), 2u * 240 + 4 * 48 + 5 * 8 + 3 * 2 + 1);
}

TEST_CASE(Xxh3AndNonce) {
  CHECK_EQ(Xxh3("", 0), 0x2D06800538D394C2ull);  // XXH3-64 test vector
  std::vector<std::complex<float>> v = {{1.0f, -2.5f}, {0.0f, 3.0e-5f}};
  unsigned char le[16];
  for (int i = 0; i != 4; ++i) {
    const float f = i == 0 ? 1.0f : i == 1 ? -2.5f : i == 2 ? 0.0f : 3.0e-5f;
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    for (int k = 0; k != 4; ++k)
      le[i * 4 + k] = static_cast<unsigned char>(u >> (8 * k));
  }
  CHECK_EQ(ChunkNonce(v.data(), v.size()), Xxh3(le, 16));
  CHECK_EQ(ChunkNonce(nullptr, 0), Xxh3("", 0));
  v[1] = {0.0f, 3.0000002e-5f};
  CHECK(ChunkNonce(v.data(), v.size()) != Xxh3(le, 16));
}

TEST_CASE(QuantizerEdges) {
  const float levels[] = {-3.0f, -1.0f, 0.0f, 1.0f, 3.0f};
  const std::size_t n = 5;
  const std::uint32_t max_dither = (1u << 31) - 1;
  CHECK_EQ(QuantizeDithered(-7.0f, levels, n, 0), 0u);
  CHECK_EQ(QuantizeDithered(-3.0f, levels, n, max_dither), 0u);
  CHECK_EQ(QuantizeDithered(3.5f, levels, n, 0), 4u);
  CHECK_EQ(QuantizeDithered(std::numeric_limits<float>::infinity(), levels, n, 0), 5u);
  CHECK_EQ(QuantizeDithered(-std::numeric_limits<float>::infinity(), levels, n, 0), 5u);
  CHECK_EQ(QuantizeDithered(std::nanf(""), levels, n, 0), 5u);
  // Midpoint between -1 and 0: mark = 2^30. The dither is compared as a
  // float, so 2^30 - 1 rounds to 2^30; 2^30 - 64 is the next float below.
  CHECK_EQ(QuantizeDithered(-0.5f, levels, n, (1u << 30) - 64), 2u);
  CHECK_EQ(QuantizeDithered(-0.5f, levels, n, (1u << 30) - 1), 1u);
  CHECK_EQ(QuantizeDithered(-0.5f, levels, n, 1u << 30), 1u);
  // Exactly on a level: the upper symbol unless the dither rounds to 2^31
  // in float (as in Dysco).
  CHECK_EQ(QuantizeDithered(1.0f, levels, n, 0), 3u);
  CHECK_EQ(QuantizeDithered(1.0f, levels, n, max_dither), 2u);
}

TEST_CASE(QuantizerIsUnbiased) {
  const float levels[] = {-1.0f, 0.0f, 0.25f, 2.0f};
  const std::uint64_t key = DitherKey(3, 4);
  const float values[] = {-0.9f, -0.1f, 0.1f, 0.3f, 1.9f};
  for (float v : values) {
    double sum = 0;
    const int n = 100000;
    for (int i = 0; i != n; ++i)
      sum += levels[QuantizeDithered(v, levels, 4,
                                     DitherValue(key, static_cast<std::uint64_t>(i)))];
    CHECK(std::fabs(sum / n - v) < 0.01);
  }
}
