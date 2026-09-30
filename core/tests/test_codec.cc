// SPDX-License-Identifier: GPL-3.0-only
#include "dp3core/dysco/codec.h"

#include <cmath>
#include <complex>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

#include "check.h"
#include "fixtures.h"
#include "dp3core/dysco/dither.h"

using namespace dp3core::dysco;
using dp3core::dysco::testing::Fixture;
using dp3core::dysco::testing::MakeFixture;

namespace {

using Bytes = std::vector<std::byte>;

Bytes Encode(const DataParams& p, const Fixture& f, unsigned threads = 1) {
  Bytes out(EncodedSize(p, f.shape, &f.map));
  EncodeData(p, f.shape, &f.map, f.data.data(), out.data(), out.size(), {threads});
  return out;
}

std::vector<std::complex<float>> Decode(const Bytes& in, const Fixture& f,
                                        const AntennaMap* map) {
  std::vector<std::complex<float>> out(f.data.size());
  DecodeData(in.data(), in.size(), f.shape, map, out.data());
  return out;
}

bool IsCross(const Fixture& f, std::size_t i) {
  const std::size_t per_row = f.shape.n_channel * f.shape.n_pol;
  const std::size_t b = (i / per_row) % f.shape.n_baseline;
  return f.map.antenna1[b] != f.map.antenna2[b];
}

double RelativeCrossError(const Fixture& f, const std::vector<std::complex<float>>& d) {
  double err = 0, ref = 0;
  for (std::size_t i = 0; i != d.size(); ++i) {
    if (!IsCross(f, i)) continue;
    err += std::norm(std::complex<double>(d[i]) - std::complex<double>(f.data[i]));
    ref += std::norm(std::complex<double>(f.data[i]));
  }
  return std::sqrt(err / ref);
}

const Normalization kAll[] = {Normalization::kRow, Normalization::kRF, Normalization::kAF};

}  // namespace

TEST_CASE(RoundTripAllNormalizations) {
  const Fixture f = MakeFixture(3, 6, 5, 4, 1);
  for (Normalization n : kAll) {
    for (unsigned bits : {4u, 8u, 12u}) {
      DataParams p;
      p.normalization = n;
      p.bits = bits;
      p.seed = 17;
      const Bytes enc = Encode(p, f);
      CHECK_EQ(enc.size(), EncodedSize(p, f.shape, &f.map));
      const double rel = RelativeCrossError(f, Decode(enc, f, &f.map));
      const double bound = bits == 4 ? 0.5 : bits == 8 ? 0.05 : 0.005;
      if (!(rel < bound)) std::cerr << ToString(n) << " " << bits << " bits: " << rel << '\n';
      CHECK(rel < bound);
    }
  }
}

TEST_CASE(AllDistributions) {
  const Fixture f = MakeFixture(1, 5, 3, 2, 2);
  for (Distribution d : {Distribution::kGaussian, Distribution::kTruncatedGaussian,
                         Distribution::kUniform}) {
    DataParams p;
    p.distribution = d;
    p.normalization = Normalization::kRF;
    p.bits = 8;
    const double rel = RelativeCrossError(f, Decode(Encode(p, f), f, &f.map));
    CHECK(rel < 0.1);
  }
}

TEST_CASE(DeterministicAcrossRunsAndThreads) {
  const Fixture f = MakeFixture(9, 5, 4, 4, 3);
  for (Normalization n : kAll) {
    DataParams p;
    p.normalization = n;
    const Bytes one = Encode(p, f, 1);
    CHECK(Encode(p, f, 1) == one);
    CHECK(Encode(p, f, 2) == one);
    CHECK(Encode(p, f, 8) == one);
    CHECK(Encode(p, f, 0) == one);
  }
  Bytes w1(EncodedSize(WeightParams{}, f.shape)), w8(w1.size());
  EncodeWeights({}, f.shape, f.weights.data(), w1.data(), w1.size(), {1});
  EncodeWeights({}, f.shape, f.weights.data(), w8.data(), w8.size(), {8});
  CHECK(w1 == w8);
}

TEST_CASE(SeedAndNonce) {
  const Fixture f = MakeFixture(2, 4, 3, 2, 4);
  DataParams p;
  p.normalization = Normalization::kRow;
  const Bytes a = Encode(p, f);
  p.seed = 1;
  const Bytes b = Encode(p, f);
  CHECK(a != b);
  const ChunkHeader ha = ReadHeader(a.data(), a.size());
  CHECK_EQ(ha.nonce, ChunkNonce(f.data.data(), f.data.size()));
  CHECK_EQ(ReadHeader(b.data(), b.size()).nonce, ha.nonce);
  // Side data (first block, right after the header) do not depend on the seed.
  const std::size_t side_bytes = 4 * ha.side_floats_per_block;
  CHECK(std::equal(a.begin() + 80, a.begin() + 80 + long(side_bytes), b.begin() + 80));
  // A different input changes the nonce.
  Fixture g = f;
  g.data[0] += 1.0f;
  const Bytes c = Encode(p, g);
  CHECK(ReadHeader(c.data(), c.size()).nonce != ha.nonce);
}

TEST_CASE(EmbeddedDictionary) {
  const Fixture f = MakeFixture(2, 4, 3, 2, 5);
  DataParams p;
  p.bits = 6;
  const Bytes plain = Encode(p, f);
  p.embed_dictionary = true;
  Bytes emb = Encode(p, f);
  CHECK_EQ(emb.size(), plain.size() + 4 * 63);
  const auto a = Decode(plain, f, &f.map), b = Decode(emb, f, &f.map);
  CHECK(std::equal(a.begin(), a.end(), b.begin()));
  emb[80 + 4 * 10] ^= std::byte{1};  // change one embedded level
  CHECK_THROWS(Decode(emb, f, &f.map), FormatError, "embedded dictionary");
}

TEST_CASE(EmbeddedAntennaMap) {
  const Fixture f = MakeFixture(2, 4, 3, 2, 6);
  DataParams p;
  p.embed_antenna_map = true;
  const Bytes emb = Encode(p, f);
  CHECK_EQ(emb.size(), EncodedSize(DataParams{}, f.shape, &f.map) + 8 * f.shape.n_baseline);
  const auto a = Decode(emb, f, nullptr), b = Decode(emb, f, &f.map);
  CHECK(std::equal(a.begin(), a.end(), b.begin()));
  AntennaMap other = f.map;
  std::swap(other.antenna2[1], other.antenna2[2]);
  CHECK_THROWS(Decode(emb, f, &other), FormatError, "differs");
  p.normalization = Normalization::kRF;
  CHECK_THROWS(Encode(p, f), std::invalid_argument, "only allowed with AF");
}

TEST_CASE(InvalidArguments) {
  const Fixture f = MakeFixture(1, 4, 2, 2, 7);
  DataParams p;
  Bytes out(EncodedSize(p, f.shape, &f.map));
  CHECK_THROWS(EncodeData(p, f.shape, nullptr, f.data.data(), out.data(), out.size()),
               std::invalid_argument, "needs an antenna map");
  AntennaMap bad = f.map;
  bad.antenna2[3] = 4;
  CHECK_THROWS(EncodeData(p, f.shape, &bad, f.data.data(), out.data(), out.size()),
               std::invalid_argument, "baseline 3");
  bad = f.map;
  bad.antenna1.pop_back();
  CHECK_THROWS(Validate(p, f.shape, &bad), std::invalid_argument, "entries for");
  CHECK_THROWS(EncodeData(p, f.shape, &f.map, f.data.data(), out.data(), out.size() - 1),
               std::invalid_argument, "needed");
  p.bits = 5;
  CHECK_THROWS(Validate(p, f.shape, &f.map), std::invalid_argument, "bit count");
  p.bits = 10;
  p.truncation = 0.0;
  CHECK_THROWS(Validate(p, f.shape, &f.map), std::invalid_argument, "truncation");
  Shape zero = f.shape;
  zero.n_channel = 0;
  CHECK_THROWS(Validate(DataParams{}, zero, &f.map), std::invalid_argument, "");
  // Row and RF need no map.
  p = DataParams{};
  p.normalization = Normalization::kRow;
  CHECK_EQ(EncodedSize(p, f.shape, nullptr), EncodedSize(p, f.shape, &f.map));
}

TEST_CASE(DecodeRejectsMismatches) {
  const Fixture f = MakeFixture(2, 4, 3, 2, 8);
  DataParams p;
  const Bytes enc = Encode(p, f);
  std::vector<std::complex<float>> out(f.data.size());
  Shape other = f.shape;
  other.n_time = 1;
  CHECK_THROWS(DecodeData(enc.data(), enc.size(), other, &f.map, out.data()), FormatError, "");
  CHECK_THROWS(DecodeData(enc.data(), enc.size() - 1, f.shape, &f.map, out.data()), FormatError, "");
  CHECK_THROWS(DecodeData(enc.data(), enc.size(), f.shape, nullptr, out.data()),
               std::invalid_argument, "needs an antenna map");
  AntennaMap bigger = f.map;
  bigger.n_antenna = 5;
  CHECK_THROWS(DecodeData(enc.data(), enc.size(), f.shape, &bigger, out.data()), FormatError,
               "n_antenna");
  std::vector<float> w(f.weights.size());
  CHECK_THROWS(DecodeWeights(enc.data(), enc.size(), f.shape, w.data()), FormatError, "");
}

TEST_CASE(NonFiniteAndZeroInput) {
  Fixture f = MakeFixture(2, 4, 3, 2, 9);
  f.data[3] = {std::numeric_limits<float>::infinity(), 1.0f};
  f.data[7] = {0.0f, std::nanf("")};
  for (std::size_t i = f.data.size() / 2; i != f.data.size(); ++i) f.data[i] = 0.0f;
  for (Normalization n : kAll) {
    DataParams p;
    p.normalization = n;
    const auto d = Decode(Encode(p, f), f, &f.map);
    CHECK(std::isnan(d[3].real()));
    CHECK(std::isnan(d[7].imag()));
    for (std::size_t i = f.data.size() / 2; i != f.data.size(); ++i)
      CHECK(d[i] == std::complex<float>(0.0f) || std::isnan(d[i].real()));
  }
  Fixture g = MakeFixture(1, 3, 2, 2, 10);
  for (auto& v : g.data) v = {std::nanf(""), std::nanf("")};
  for (Normalization n : kAll) {
    DataParams p;
    p.normalization = n;
    const auto d = Decode(Encode(p, g), g, &g.map);
    for (const auto& v : d) CHECK(std::isnan(v.real()) && std::isnan(v.imag()));
  }
}

TEST_CASE(DitherIsUnbiased) {
  // Many noisy copies of a fixed row: the mean decoded value converges to
  // the input (4 bits, Row normalization, one baseline).
  Shape shape{400, 1, 8, 1};
  std::vector<std::complex<float>> in;
  for (std::size_t t = 0; t != shape.n_time; ++t)
    for (std::size_t ch = 0; ch != 8; ++ch) in.emplace_back(0.1f * float(ch) - 0.33f, 0.21f);
  for (auto& v : in) v += std::complex<float>(0.0f, 0.0f);
  in[7] = {1.0f, 1.0f};  // block 0 max (other blocks have max |re| 0.37)
  DataParams p;
  p.normalization = Normalization::kRow;
  p.bits = 4;
  Bytes enc(EncodedSize(p, shape, nullptr));
  EncodeData(p, shape, nullptr, in.data(), enc.data(), enc.size());
  std::vector<std::complex<float>> out(in.size());
  DecodeData(enc.data(), enc.size(), shape, nullptr, out.data());
  for (std::size_t ch = 0; ch != 7; ++ch) {
    double sum = 0;
    for (std::size_t t = 1; t != shape.n_time; ++t) sum += out[t * 8 + ch].real();
    const double mean = sum / double(shape.n_time - 1);
    const double expected = 0.1 * double(ch) - 0.33;
    CHECK(std::fabs(mean - expected) < 0.01);
  }
}

TEST_CASE(WeightsRoundTrip) {
  const Fixture f = MakeFixture(3, 5, 4, 4, 11);
  WeightParams wp;
  Bytes enc(EncodedSize(wp, f.shape));
  EncodeWeights(wp, f.shape, f.weights.data(), enc.data(), enc.size());
  std::vector<float> out(f.weights.size());
  DecodeWeights(enc.data(), enc.size(), f.shape, out.data());
  const std::size_t per_bc = f.shape.n_pol;
  for (std::size_t i = 0; i != out.size(); i += per_bc) {
    float mn = f.weights[i];
    for (std::size_t p = 1; p != per_bc; ++p) mn = std::min(mn, f.weights[i + p]);
    for (std::size_t p = 0; p != per_bc; ++p) CHECK(std::fabs(out[i + p] - mn) <= 0.25f * 0.5f);
  }
  std::vector<float> bad = f.weights;
  bad[5] = -1.0f;
  CHECK_THROWS(EncodeWeights(wp, f.shape, bad.data(), enc.data(), enc.size(), {4}),
               std::invalid_argument, "index 5");
}

TEST_CASE(GoldenFixtures) {
  // XXH3-64 of the encoded chunk and of the decoded float32 values. Must be
  // identical on every platform, compiler and thread count.
  const Fixture f = MakeFixture(3, 5, 4, 4, 12);
  const auto& expected = testing::kGolden;
  std::uint64_t got[4][2];
  int k = 0;
  for (Normalization n : kAll) {
    DataParams p;
    p.normalization = n;
    p.seed = testing::kGoldenSeed;
    const Bytes enc = Encode(p, f, 3);
    const auto dec = Decode(enc, f, &f.map);
    got[k][0] = Xxh3(enc.data(), enc.size());
    got[k][1] = Xxh3(dec.data(), dec.size() * 8);
    ++k;
  }
  Bytes w(EncodedSize(WeightParams{}, f.shape));
  EncodeWeights({}, f.shape, f.weights.data(), w.data(), w.size());
  std::vector<float> wd(f.weights.size());
  DecodeWeights(w.data(), w.size(), f.shape, wd.data());
  got[3][0] = Xxh3(w.data(), w.size());
  got[3][1] = Xxh3(wd.data(), wd.size() * 4);
  bool all = true;
  for (int i = 0; i != 4; ++i) {
    std::printf("{0x%016llxull, 0x%016llxull},\n", static_cast<unsigned long long>(got[i][0]),
                static_cast<unsigned long long>(got[i][1]));
    all = all && got[i][0] == expected[i][0] && got[i][1] == expected[i][1];
  }
  CHECK(all);
}
