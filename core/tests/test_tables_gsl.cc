// Compares the portable tables with tables built the way the original Dysco
// builds them (GSL + long double). Built only when GSL is found.
// SPDX-License-Identifier: GPL-3.0-only
//
// The reference construction below follows aroffringa/dysco @ aa30453,
// stochasticencoder.cc (StochasticEncoder constructor, cumulative,
// invCumulative, initializeTruncatedGaussian) and dyscodatacolumn.cc
// (unit RMS). GPL-3.0, like this library.
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_sf_erf.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <vector>

#include "check.h"
#include "dp3core/dysco/tables.h"

using namespace dp3core::dysco;

namespace {
using num_t = long double;
#ifndef M_SQRT2l
#define M_SQRT2l 1.4142135623730950488016887242096981L
#endif

num_t Cumulative(num_t x) {
  return num_t(0.5) + num_t(0.5) * gsl_sf_erf(static_cast<double>(x / num_t(M_SQRT2l)));
}

num_t InvCumulative(num_t c, num_t err = num_t(1e-13)) {
  if (c < 0.5) return -InvCumulative(1.0L - c, err);
  if (c == 0.5) return 0.0L;
  num_t x = 1.0L, fx = Cumulative(x), x_low, x_high;
  if (fx < c) {
    do {
      x *= 2.0L;
      fx = Cumulative(x);
    } while (fx < c);
    x_low = x * 0.5L;
    x_high = x;
  } else {
    x_low = 0.0L;
    x_high = 1.0L;
  }
  num_t error = x_high;
  int not_converging = 0;
  do {
    x = (x_low + x_high) * 0.5L;
    fx = Cumulative(x);
    if (fx > c)
      x_high = x;
    else
      x_low = x;
    const num_t current = std::fabs(fx - c);
    if (current >= error) {
      if (++not_converging > 10) return x;
    } else {
      not_converging = 0;
    }
    error = current;
  } while (error > err);
  return x;
}

std::vector<float> DyscoReferenceTable(unsigned bits, Distribution d, double trunc) {
  const std::size_t quant_count = std::size_t{1} << bits;
  std::vector<float> table(quant_count - 1);
  if (d == Distribution::kTruncatedGaussian) {
    const double cdf_at_trunc = gsl_cdf_gaussian_P(-trunc, 1.0);
    const double factor = 1.0 - 2.0 * cdf_at_trunc;
    for (std::size_t i = 0; i != quant_count - 1; ++i) {
      const double val = static_cast<double>(
          (static_cast<num_t>(i) + num_t(0.5)) / static_cast<num_t>(quant_count - 1));
      table[i] = static_cast<float>(gsl_cdf_gaussian_Pinv(val * factor + cdf_at_trunc, 1.0));
    }
    return table;
  }
  float stddev = 1.0f;
  stddev = static_cast<float>(stddev * std::sqrt(3.0));
  for (std::size_t i = 0; i != quant_count - 1; ++i) {
    const num_t val =
        (static_cast<num_t>(i) + num_t(0.5)) / static_cast<num_t>(quant_count - 1);
    if (d == Distribution::kGaussian)
      table[i] = static_cast<float>(stddev * InvCumulative(val));
    else
      table[i] = static_cast<float>(stddev * (-1.0L + 2.0L * val));
  }
  return table;
}

// Distance in units in the last place between two finite floats.
std::int64_t UlpDistance(float a, float b) {
  auto key = [](float f) {
    std::int32_t i;
    std::memcpy(&i, &f, 4);
    return i < 0 ? std::int64_t{INT32_MIN} - i : std::int64_t{i};
  };
  const std::int64_t d = key(a) - key(b);
  return d < 0 ? -d : d;
}
}  // namespace

TEST_CASE(PortableTablesMatchDyscoWithinTwoUlp) {
  const unsigned all_bits[] = {2, 3, 4, 6, 8, 10, 12, 16};
  struct Case {
    Distribution d;
    double trunc;
  };
  const Case cases[] = {{Distribution::kGaussian, 0.0},
                        {Distribution::kUniform, 0.0},
                        {Distribution::kTruncatedGaussian, 1.5},
                        {Distribution::kTruncatedGaussian, 2.5},
                        {Distribution::kTruncatedGaussian, 4.0}};
  std::cout << "  long double has " << std::numeric_limits<long double>::digits
            << " mantissa bits here\n";
  for (const Case& c : cases) {
    std::map<std::int64_t, std::size_t> histogram;
    for (unsigned bits : all_bits) {
      if (bits == 16 && c.d == Distribution::kTruncatedGaussian && c.trunc != 2.5)
        continue;  // keep the test fast
      const std::vector<float> ours = DecodeTable(bits, c.d, c.trunc);
      const std::vector<float> ref = DyscoReferenceTable(bits, c.d, c.trunc);
      CHECK_EQ(ours.size(), ref.size());
      for (std::size_t i = 0; i != ours.size(); ++i)
        ++histogram[UlpDistance(ours[i], ref[i])];
    }
    std::cout << "  " << ToString(c.d) << " k=" << c.trunc << ": ulp histogram";
    for (const auto& [ulp, count] : histogram) std::cout << ' ' << ulp << ':' << count;
    std::cout << '\n';
    CHECK(histogram.rbegin()->first <= 2);
  }
}
