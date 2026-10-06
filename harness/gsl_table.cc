// SPDX-License-Identifier: GPL-3.0-only
#include "gsl_table.h"

#include <cmath>
#include <stdexcept>

#if defined(DP3CORE_DYSCO_HAVE_GSL)
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_sf_erf.h>
#endif

namespace dp3core::dysco::harness {
namespace {

#if defined(DP3CORE_DYSCO_HAVE_GSL)
using num_t = long double;
#ifndef M_SQRT2l
#define M_SQRT2l 1.4142135623730950488016887242096981L
#endif

num_t Cumulative(num_t x) {
  return num_t(0.5) + num_t(0.5) * static_cast<num_t>(gsl_sf_erf(static_cast<double>(x / num_t(M_SQRT2l))));
}

num_t InvCumulative(num_t c, num_t err = num_t(1e-13)) {
  if (c < 0.5L) return -InvCumulative(1.0L - c, err);
  if (c == 0.5L) return 0.0L;
  num_t x = 1.0L;
  num_t fx = Cumulative(x);
  num_t x_low = 0.0L;
  num_t x_high = 1.0L;
  if (fx < c) {
    do {
      x *= 2.0L;
      fx = Cumulative(x);
    } while (fx < c);
    x_low = x * 0.5L;
    x_high = x;
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
#endif

}  // namespace

bool GslAvailable() {
#if defined(DP3CORE_DYSCO_HAVE_GSL)
  return true;
#else
  return false;
#endif
}

std::vector<float> GslDecodeTable(unsigned bits, Distribution distribution, double truncation) {
#if !defined(DP3CORE_DYSCO_HAVE_GSL)
  (void)bits;
  (void)distribution;
  (void)truncation;
  throw std::runtime_error("GSL is not linked into this harness");
#else
  if (!IsSupportedBitCount(bits)) throw std::runtime_error("unsupported bit count for the GSL table");
  const std::size_t quant_count = std::size_t{1} << bits;
  std::vector<float> table(quant_count - 1);
  if (distribution == Distribution::kTruncatedGaussian) {
    const double cdf_at_trunc = gsl_cdf_gaussian_P(-truncation, 1.0);
    const double factor = 1.0 - 2.0 * cdf_at_trunc;
    for (std::size_t i = 0; i != quant_count - 1; ++i) {
      const double val = static_cast<double>(
          (static_cast<num_t>(i) + num_t(0.5)) / static_cast<num_t>(quant_count - 1));
      table[i] = static_cast<float>(gsl_cdf_gaussian_Pinv(val * factor + cdf_at_trunc, 1.0));
    }
    return table;
  }
  const float scale = static_cast<float>(std::sqrt(3.0));
  for (std::size_t i = 0; i != quant_count - 1; ++i) {
    const num_t val =
        (static_cast<num_t>(i) + num_t(0.5)) / static_cast<num_t>(quant_count - 1);
    if (distribution == Distribution::kGaussian)
      table[i] = static_cast<float>(scale * InvCumulative(val));
    else
      table[i] = static_cast<float>(scale * (-1.0L + 2.0L * val));
  }
  return table;
#endif
}

}  // namespace dp3core::dysco::harness
