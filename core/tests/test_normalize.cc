// SPDX-License-Identifier: GPL-3.0-only
#include "normalize.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "check.h"
#include "dp3core/dysco/dither.h"
#include "dp3core/dysco/tables.h"

using namespace dp3core::dysco;
using detail::BlockLayout;

namespace {

struct Block {
  std::vector<std::uint32_t> a1, a2;
  std::vector<std::complex<float>> data;
  BlockLayout layout;
};

// Uniform noise (basic operations only, so identical on every platform),
// scaled by per-antenna gains and per-channel factors; autos are 20x larger.
Block MakeBlock(std::size_t n_ant, std::size_t n_chan, std::size_t n_pol,
                std::uint64_t seed) {
  Block b;
  for (std::uint32_t i = 0; i != n_ant; ++i)
    for (std::uint32_t j = i; j != n_ant; ++j) {
      b.a1.push_back(i);
      b.a2.push_back(j);
    }
  std::uint64_t s = seed;
  auto u = [&s]() { return double(Mix64(++s) >> 11) / double(1ull << 53); };
  std::vector<double> gain(n_ant), chan(n_chan * n_pol);
  for (auto& g : gain) g = 0.5 + 2.0 * u();
  for (auto& c : chan) c = 0.5 + u();
  for (std::size_t r = 0; r != b.a1.size(); ++r)
    for (std::size_t ch = 0; ch != n_chan; ++ch)
      for (std::size_t p = 0; p != n_pol; ++p) {
        const double scale = gain[b.a1[r]] * gain[b.a2[r]] * chan[ch * n_pol + p] *
                             (b.a1[r] == b.a2[r] ? 20.0 : 1.0);
        // Sequenced: function-argument evaluation order is unspecified.
        const float re = float(scale * (u() - 0.5));
        const float im = float(scale * (u() - 0.5));
        b.data.emplace_back(re, im);
      }
  b.layout.n_baseline = b.a1.size();
  b.layout.n_channel = n_chan;
  b.layout.n_pol = n_pol;
  b.layout.n_antenna = n_ant;
  b.layout.antenna1 = b.a1.data();
  b.layout.antenna2 = b.a2.data();
  return b;
}

std::size_t SideCount(Normalization n, const BlockLayout& l) {
  switch (n) {
    case Normalization::kRow: return l.n_baseline;
    case Normalization::kRF: return l.n_pol * (l.n_channel + l.n_baseline);
    case Normalization::kAF: return l.n_pol * (l.n_channel + l.n_antenna);
  }
  return 0;
}

void Normalize(Normalization n, std::complex<double>* d, const BlockLayout& l,
               double max_level, float* side) {
  switch (n) {
    case Normalization::kRow: detail::NormalizeRow(d, l, max_level, side); break;
    case Normalization::kRF: detail::NormalizeRF(d, l, max_level, side); break;
    case Normalization::kAF: detail::NormalizeAF(d, l, max_level, side); break;
  }
}

void Denormalize(Normalization n, const std::uint32_t* sym, const float* levels,
                 const BlockLayout& l, const float* side, std::complex<float>* out) {
  switch (n) {
    case Normalization::kRow: detail::DenormalizeRow(sym, levels, l, side, out); break;
    case Normalization::kRF: detail::DenormalizeRF(sym, levels, l, side, out); break;
    case Normalization::kAF: detail::DenormalizeAF(sym, levels, l, side, out); break;
  }
}

struct Result {
  std::vector<float> side;
  std::vector<std::complex<double>> normalized;
  std::vector<std::uint32_t> symbols;
  std::vector<std::complex<float>> decoded;
};

Result RoundTrip(Normalization n, const Block& b, unsigned bits) {
  std::vector<float> levels = DecodeTable(bits, Distribution::kTruncatedGaussian, 2.5);
  const std::size_t n_levels = levels.size();
  levels.push_back(std::numeric_limits<float>::quiet_NaN());
  Result r;
  r.side.assign(SideCount(n, b.layout), -1.0f);
  r.normalized.assign(b.data.begin(), b.data.end());
  Normalize(n, r.normalized.data(), b.layout, levels[n_levels - 1], r.side.data());
  const std::uint64_t key = DitherKey(42, ChunkNonce(b.data.data(), b.data.size()));
  for (std::size_t i = 0; i != r.normalized.size(); ++i) {
    r.symbols.push_back(QuantizeDithered(float(r.normalized[i].real()), levels.data(),
                                         n_levels, DitherValue(key, 2 * i)));
    r.symbols.push_back(QuantizeDithered(float(r.normalized[i].imag()), levels.data(),
                                         n_levels, DitherValue(key, 2 * i + 1)));
  }
  r.decoded.resize(b.data.size());
  Denormalize(n, r.symbols.data(), levels.data(), b.layout, r.side.data(), r.decoded.data());
  return r;
}

const Normalization kAll[] = {Normalization::kRow, Normalization::kRF, Normalization::kAF};

}  // namespace

TEST_CASE(RowScalesEachRowToMaxLevel) {
  const Block b = MakeBlock(5, 3, 2, 1);
  std::vector<std::complex<double>> d(b.data.begin(), b.data.end());
  std::vector<float> side(b.layout.n_baseline);
  detail::NormalizeRow(d.data(), b.layout, 2.0, side.data());
  const std::size_t vpr = 6;
  for (std::size_t r = 0; r != b.layout.n_baseline; ++r) {
    double in_max = 0, out_max = 0;
    for (std::size_t i = 0; i != vpr; ++i) {
      in_max = std::max({in_max, std::fabs(double(b.data[r * vpr + i].real())),
                         std::fabs(double(b.data[r * vpr + i].imag()))});
      out_max = std::max({out_max, std::fabs(d[r * vpr + i].real()), std::fabs(d[r * vpr + i].imag())});
    }
    CHECK(std::fabs(out_max - 2.0) < 1e-12);
    CHECK_EQ(side[r], float(in_max / 2.0));
  }
}

TEST_CASE(NormalizedValuesStayInRange) {
  const Block b = MakeBlock(7, 4, 4, 2);
  for (Normalization n : {Normalization::kRow, Normalization::kRF}) {
    const Result r = RoundTrip(n, b, 8);
    const double max_level = DecodeTable(8, Distribution::kTruncatedGaussian, 2.5).back();
    for (const auto& v : r.normalized) {
      CHECK(std::fabs(v.real()) <= max_level * (1 + 1e-12));
      CHECK(std::fabs(v.imag()) <= max_level * (1 + 1e-12));
    }
  }
}

TEST_CASE(RoundTripErrorIsSmall) {
  // Relative RMS error of cross-correlations for 8 and 16 bits.
  const Block b = MakeBlock(9, 6, 4, 3);
  for (Normalization n : kAll) {
    for (unsigned bits : {8u, 16u}) {
      const Result r = RoundTrip(n, b, bits);
      double err = 0, ref = 0;
      for (std::size_t i = 0; i != b.data.size(); ++i) {
        if (b.a1[i / 24] == b.a2[i / 24]) continue;
        err += std::norm(std::complex<double>(r.decoded[i]) - std::complex<double>(b.data[i]));
        ref += std::norm(std::complex<double>(b.data[i]));
      }
      const double rel = std::sqrt(err / ref);
      const double bound = bits == 8 ? 0.05 : 0.001;
      if (!(rel < bound))
        std::cerr << ToString(n) << " bits " << bits << " rel error " << rel << '\n';
      CHECK(rel < bound);
    }
  }
}

TEST_CASE(NonFiniteValuesDecodeToNaN) {
  Block b = MakeBlock(4, 2, 2, 4);
  b.data[5] = {std::numeric_limits<float>::infinity(), 1.0f};
  b.data[9] = {0.0f, std::nanf("")};
  for (Normalization n : kAll) {
    const Result r = RoundTrip(n, b, 10);
    CHECK(std::isnan(r.decoded[5].real()));
    CHECK(!std::isnan(r.decoded[5].imag()));
    CHECK(std::isnan(r.decoded[9].imag()));
    for (float s : r.side) CHECK(std::isfinite(s));
  }
}

TEST_CASE(ZeroBlockDecodesToZero) {
  Block b = MakeBlock(4, 2, 2, 5);
  for (auto& v : b.data) v = 0.0f;
  for (Normalization n : kAll) {
    const Result r = RoundTrip(n, b, 6);
    for (const auto& v : r.decoded) {
      CHECK(v.real() == 0.0f || std::isnan(v.real()));
      CHECK(v.imag() == 0.0f || std::isnan(v.imag()));
    }
  }
}

TEST_CASE(GoldenFixtures) {
  // XXH3-64 of side data (float32 LE) and symbols (u32 LE) for a fixed block.
  // Must be identical on every platform and compiler.
  const Block b = MakeBlock(6, 5, 4, 7);
  const std::uint64_t expected[3][2] = {
      {0x282956af7a9cfd88ull, 0xbcde40080a867d2aull},   // row
      {0xb58e3154c9643750ull, 0xaddc203941517290ull},   // rf
      {0xc3068def5e54182dull, 0xd610b7988ddf16fcull}};  // af
  int k = 0;
  bool all_match = true;
  for (Normalization n : kAll) {
    const Result r = RoundTrip(n, b, 10);
    const std::uint64_t side_hash = Xxh3(r.side.data(), r.side.size() * 4);
    const std::uint64_t sym_hash = Xxh3(r.symbols.data(), r.symbols.size() * 4);
    std::printf("%s: {0x%016llxull, 0x%016llxull}\n", ToString(n),
                static_cast<unsigned long long>(side_hash),
                static_cast<unsigned long long>(sym_hash));
    all_match = all_match && side_hash == expected[k][0] && sym_hash == expected[k][1];
    ++k;
  }
  CHECK(all_match);
}
