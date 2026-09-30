// The shared test fixture and its golden hashes. The Python tests
// (python/tests/fixtures.py) and the TensorStore test use the same values.
// SPDX-License-Identifier: GPL-3.0-only
#ifndef DP3CORE_DYSCO_TESTS_FIXTURES_H_
#define DP3CORE_DYSCO_TESTS_FIXTURES_H_

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include "dp3core/dysco/codec.h"
#include "dp3core/dysco/dither.h"

namespace dp3core::dysco::testing {

struct Fixture {
  Shape shape;
  AntennaMap map;
  std::vector<std::complex<float>> data;
  std::vector<float> weights;
};

// Uniform noise with antenna gains and channel factors; autos 20x. Basic
// operations only, so the data are identical on every platform.
inline Fixture MakeFixture(std::size_t n_time, std::uint32_t n_ant, std::size_t n_chan,
                    std::size_t n_pol, std::uint64_t seed) {
  Fixture f;
  f.map.n_antenna = n_ant;
  for (std::uint32_t i = 0; i != n_ant; ++i)
    for (std::uint32_t j = i; j != n_ant; ++j) {
      f.map.antenna1.push_back(i);
      f.map.antenna2.push_back(j);
    }
  f.shape = {n_time, f.map.antenna1.size(), n_chan, n_pol};
  std::uint64_t s = seed;
  auto u = [&s]() { return double(Mix64(++s) >> 11) / double(1ull << 53); };
  std::vector<double> gain(n_ant), chan(n_chan * n_pol);
  for (auto& g : gain) g = 0.5 + 2.0 * u();
  for (auto& c : chan) c = 0.5 + u();
  for (std::size_t t = 0; t != n_time; ++t)
    for (std::size_t b = 0; b != f.shape.n_baseline; ++b)
      for (std::size_t ch = 0; ch != n_chan; ++ch)
        for (std::size_t p = 0; p != n_pol; ++p) {
          const bool is_auto = f.map.antenna1[b] == f.map.antenna2[b];
          const double scale = gain[f.map.antenna1[b]] * gain[f.map.antenna2[b]] *
                               chan[ch * n_pol + p] * (is_auto ? 20.0 : 1.0) *
                               (1.0 + 0.1 * double(t));
          const float re = float(scale * (u() - 0.5));
          const float im = float(scale * (u() - 0.5));
          f.data.emplace_back(re, im);
          f.weights.push_back(float(std::floor(u() * 1000.0)) * 0.25f);
        }
  return f;
}

// GoldenFixtures: MakeFixture(3, 5, 4, 4, 12), seed 0x5EED, default
// bits/distribution/truncation. {XXH3-64 of the encoded chunk, XXH3-64 of the
// decoded values} for row, rf, af and weights.
inline constexpr std::uint64_t kGoldenSeed = 0x5EED;
inline constexpr std::uint64_t kGolden[4][2] = {
    {0x1ba8547955090c7eull, 0x0c10d3c2e1b56d65ull},   // row
    {0x2de51a6aae790c1aull, 0x3948d4be8db707bdull},   // rf
    {0xc9817c878bf2af2aull, 0x24895812c0a32df7ull},   // af
    {0x1d3d8efd2346bf58ull, 0x1781051cac6f0bbeull}};  // weights

}  // namespace dp3core::dysco::testing

#endif  // DP3CORE_DYSCO_TESTS_FIXTURES_H_
