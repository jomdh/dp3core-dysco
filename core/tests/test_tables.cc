// Tests for the portable quantization tables (spec/SPEC.md section 4).
// SPDX-License-Identifier: GPL-3.0-only
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>

#include "check.h"
#include "dp3core/dysco/tables.h"
#include "golden_tables.h"

using namespace dp3core::dysco;

namespace {
bool Near(double a, double b, double rel) {
  return std::fabs(a - b) <= rel * std::fabs(b);
}

const unsigned kAllBits[] = {2, 3, 4, 6, 8, 10, 12, 16};
const Distribution kAllDistributions[] = {
    Distribution::kGaussian, Distribution::kTruncatedGaussian,
    Distribution::kUniform};
}  // namespace

TEST_CASE(ExpMatchesReferenceValues) {
  CHECK(portable::Exp(0.0) == 1.0);
  CHECK(Near(portable::Exp(-1.0), 0.36787944117144233, 4e-16));
  CHECK(Near(portable::Exp(-0.3), 0.7408182206817179, 4e-16));
  CHECK(Near(portable::Exp(-10.0), 4.5399929762484854e-05, 4e-16));
  CHECK(portable::Exp(-800.0) == 0.0);
  CHECK_THROWS(portable::Exp(0.5), std::invalid_argument, "x must be <= 0");
}

TEST_CASE(ErfcMatchesReferenceValues) {
  CHECK(portable::Erfc(0.0) == 1.0);
  CHECK(Near(portable::Erfc(0.5), 0.4795001221869535, 1e-14));
  CHECK(Near(portable::Erfc(1.0), 0.15729920705028513, 1e-14));
  CHECK(Near(portable::Erfc(2.0), 0.004677734981047265, 1e-14));
  CHECK(Near(portable::Erfc(3.0), 2.2090496998585438e-05, 1e-14));
  CHECK(Near(portable::Erfc(5.0), 1.5374597944280351e-12, 1e-14));
  CHECK_THROWS(portable::Erfc(-1.0), std::invalid_argument, "z must be >= 0");
}

TEST_CASE(PhiAndInverse) {
  CHECK(Near(portable::PhiLower(-1.0), 0.15865525393145705, 1e-14));
  CHECK(Near(portable::PhiLower(-2.5), 0.0062096653257761352, 1e-14));
  CHECK(portable::PhiLower(0.0) == 0.5);
  CHECK(portable::InversePhiLower(0.5) == 0.0);
  for (int i = 1; i <= 800; ++i) {
    const double x = -i * 0.01;
    const double back = portable::InversePhiLower(portable::PhiLower(x));
    if (std::fabs(back - x) > 1e-12 * (1.0 + std::fabs(x))) {
      CHECK(std::fabs(back - x) <= 1e-12 * (1.0 + std::fabs(x)));
      break;
    }
  }
  CHECK_THROWS(portable::InversePhiLower(0.6), std::invalid_argument, "(0, 0.5]");
  CHECK_THROWS(portable::InversePhiLower(0.0), std::invalid_argument, "(0, 0.5]");
}

TEST_CASE(TableShapeMonotonicSymmetric) {
  for (unsigned bits : kAllBits) {
    for (Distribution d : kAllDistributions) {
      const std::vector<float> t = DecodeTable(bits, d, 2.5);
      const std::size_t n = (std::size_t{1} << bits) - 1;
      CHECK_EQ(t.size(), n);
      const std::size_t m = (n - 1) / 2;
      bool ok = t[m] == 0.0f && !std::signbit(t[m]);
      for (std::size_t i = 0; i != n; ++i) {
        ok = ok && std::isfinite(t[i]) && t[i] == -t[n - 1 - i];
        if (i + 1 != n) ok = ok && t[i] < t[i + 1];
      }
      if (!ok) {
        std::cerr << "bits " << bits << ' ' << ToString(d) << '\n';
        CHECK(ok);
      }
    }
  }
}

TEST_CASE(TableKnownValues) {
  // 2 bits: three levels at u = 1/6, 1/2, 5/6.
  const float sqrt3f = 1.7320508075688772f;
  const std::vector<float> g = DecodeTable(2, Distribution::kGaussian, 0.0);
  // Phi^-1(1/6) = -0.96742156610170104...
  CHECK_EQ(g[0], static_cast<float>(double(sqrt3f) * -0.96742156610170104));
  const std::vector<float> u = DecodeTable(2, Distribution::kUniform, 0.0);
  CHECK_EQ(u[0], static_cast<float>(double(sqrt3f) * -2.0 / 3.0));
  const std::vector<float> tg =
      DecodeTable(2, Distribution::kTruncatedGaussian, 2.5);
  // p = Phi(-2.5) + (1/6)(1 - 2 Phi(-2.5)) = 0.170806443550517...
  // Phi^-1(p) = -0.950983233704274... (Python statistics.NormalDist).
  CHECK_EQ(tg[0], static_cast<float>(-0.950983233704274));
  // Largest 10-bit level (the clipping limit of the normalizations):
  // Phi^-1(Phi(-2.5) + (2045/2046)(1 - 2 Phi(-2.5))) = 2.47336576429316...
  CHECK_EQ(DecodeTable(10, Distribution::kTruncatedGaussian, 2.5).back(),
           static_cast<float>(2.47336576429316));
}

TEST_CASE(TableArgumentChecks) {
  CHECK_THROWS(DecodeTable(5, Distribution::kGaussian, 0.0),
               std::invalid_argument, "unsupported bit count 5");
  CHECK_THROWS(DecodeTable(1, Distribution::kGaussian, 0.0),
               std::invalid_argument, "unsupported bit count");
  CHECK_THROWS(DecodeTable(10, Distribution::kTruncatedGaussian, 0.0),
               std::invalid_argument, "truncation must be in (0, 8]");
  CHECK_THROWS(DecodeTable(10, Distribution::kTruncatedGaussian, 9.0),
               std::invalid_argument, "truncation");
  CHECK_THROWS(DecodeTable(10, Distribution::kTruncatedGaussian,
                           std::numeric_limits<double>::quiet_NaN()),
               std::invalid_argument, "truncation");
  CHECK_THROWS(DecodeTable(10, static_cast<Distribution>(7), 0.0),
               std::invalid_argument, "invalid distribution");
  // Truncation is ignored for the other distributions.
  CHECK(DecodeTable(4, Distribution::kGaussian, -3.0) ==
        DecodeTable(4, Distribution::kGaussian, 2.5));
}

TEST_CASE(TableChecksumIsLittleEndianXxh3) {
  // Empty table: XXH3-64 of zero bytes.
  CHECK_EQ(TableChecksum({}), std::uint64_t{0x2D06800538D394C2ull});
  // Changing one level changes the checksum.
  std::vector<float> t = DecodeTable(4, Distribution::kGaussian, 0.0);
  const std::uint64_t before = TableChecksum(t);
  t[3] = std::nextafter(t[3], 0.0f);
  CHECK(TableChecksum(t) != before);
}

// The golden checksums pin the exact float bits of every table. They must be
// identical on every platform and compiler; a difference is a portability bug.
TEST_CASE(TableGoldenChecksums) {
  for (const GoldenTable& g : kGoldenTables) {
    const std::uint64_t got =
        TableChecksum(DecodeTable(g.bits, g.distribution, g.truncation));
    if (got != g.checksum) {
      std::cerr << "bits " << g.bits << ' ' << ToString(g.distribution) << ' '
                << g.truncation << ": got 0x" << std::hex << got
                << " expected 0x" << g.checksum << std::dec << '\n';
      CHECK(got == g.checksum);
    }
  }
}

TEST_CASE(TableBuildTime) {
  const auto start = std::chrono::steady_clock::now();
  (void)DecodeTable(16, Distribution::kTruncatedGaussian, 2.5);
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - start)
                             .count();
  std::cout << "  16-bit truncated-Gaussian table built in " << seconds
            << " s\n";
  CHECK(seconds < 30.0);
}
