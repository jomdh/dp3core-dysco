// SPDX-License-Identifier: GPL-3.0-only
#include "weights.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "check.h"

using namespace dp3core::dysco::detail;

TEST_CASE(HandVector) {
  // 2 baselines x 2 channels x 2 pols, Q = 16.
  const std::vector<float> w = {3, 2, 4, 5, 0, 1, 7.5f, 8};
  float side = -1;
  std::vector<std::uint32_t> sym(4);
  EncodeWeightBlock(w.data(), 2, 2, 2, 16, &side, sym.data());
  CHECK_EQ(side, 7.5f);  // max of per-pol minima {2, 4, 0, 7.5}
  CHECK_EQ(sym[0], 4u);  // round(2 * 15 / 7.5)
  CHECK_EQ(sym[1], 8u);
  CHECK_EQ(sym[2], 0u);
  CHECK_EQ(sym[3], 15u);
  std::vector<float> out(8);
  DecodeWeightBlock(sym.data(), 2, 2, 2, 16, side, out.data());
  CHECK_EQ(out[0], 2.0f);
  CHECK_EQ(out[1], 2.0f);
  CHECK_EQ(out[2], 4.0f);
  CHECK_EQ(out[4], 0.0f);
  CHECK_EQ(out[7], 7.5f);
}

TEST_CASE(AllZeroUsesMaxOne) {
  const std::vector<float> w(12, 0.0f);
  float side = -1;
  std::vector<std::uint32_t> sym(6, 99);
  EncodeWeightBlock(w.data(), 3, 2, 2, 256, &side, sym.data());
  CHECK_EQ(side, 1.0f);
  for (auto s : sym) CHECK_EQ(s, 0u);
}

TEST_CASE(RoundingHalfAwayFromZero) {
  // w/max * (Q-1) = 0.5 and 1.5 exactly.
  const std::vector<float> w = {0.5f, 1.5f, 3.0f};
  float side;
  std::vector<std::uint32_t> sym(3);
  EncodeWeightBlock(w.data(), 1, 3, 1, 4, &side, sym.data());
  CHECK_EQ(sym[0], 1u);
  CHECK_EQ(sym[1], 2u);
  CHECK_EQ(sym[2], 3u);
}

TEST_CASE(RelativeErrorBound) {
  std::vector<float> w;
  for (int i = 0; i != 1000; ++i) w.push_back(float(i % 97) * 0.37f + 0.01f);
  float side;
  std::vector<std::uint32_t> sym(1000);
  std::vector<float> out(1000);
  EncodeWeightBlock(w.data(), 10, 100, 1, 1u << 12, &side, sym.data());
  DecodeWeightBlock(sym.data(), 10, 100, 1, 1u << 12, side, out.data());
  for (int i = 0; i != 1000; ++i)
    CHECK(std::fabs(out[size_t(i)] - w[size_t(i)]) <= 0.5f * side / 4095.0f * 1.0001f);
}

TEST_CASE(RejectsBadInput) {
  float side;
  std::vector<std::uint32_t> sym(1);
  const float neg[] = {1.0f, -0.5f};
  const float nan[] = {std::nanf(""), 1.0f};
  const float inf[] = {1.0f, std::numeric_limits<float>::infinity()};
  CHECK_THROWS(EncodeWeightBlock(neg, 1, 1, 2, 16, &side, sym.data()), std::invalid_argument, "negative or not finite");
  CHECK_THROWS(EncodeWeightBlock(nan, 1, 1, 2, 16, &side, sym.data()), std::invalid_argument, "index 0");
  CHECK_THROWS(EncodeWeightBlock(inf, 1, 1, 2, 16, &side, sym.data()), std::invalid_argument, "index 1");
}
