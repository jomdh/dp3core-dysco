// SPDX-License-Identifier: GPL-3.0-only
#include "stats.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#if defined(DP3CORE_DYSCO_HAVE_GSL)
#include <gsl/gsl_cdf.h>
#endif

namespace dp3core::dysco::harness {
namespace {

constexpr double kMadToSigma = 1.4826;
constexpr std::size_t kMinDifferences = 20;

double MedianInPlace(std::vector<float>& values) {
  if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
  const std::size_t n = values.size();
  const std::size_t mid = n / 2;
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
  const double upper = static_cast<double>(values[mid]);
  if (n % 2 == 1) return upper;
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid - 1),
                   values.begin() + static_cast<std::ptrdiff_t>(mid));
  return 0.5 * (static_cast<double>(values[mid - 1]) + upper);
}

std::size_t HistogramBin(double abs_error) {
  const std::vector<double>& edges = AbsErrorBins();
  if (!(abs_error >= 0.0)) return 0;
  // First edge strictly greater than abs_error, then one step back.
  // The last edge is +inf, so an infinite error lands in the last bin.
  const auto it = std::upper_bound(edges.begin(), edges.end(), abs_error);
  const auto index = static_cast<std::size_t>(it - edges.begin());
  if (index == 0) return 0;
  return index - 1;
}

}  // namespace

std::int64_t UlpDistance(float a, float b) {
  auto key = [](float f) {
    std::int32_t i = 0;
    std::memcpy(&i, &f, sizeof(i));
    return i < 0 ? static_cast<std::int64_t>(INT32_MIN) - i : static_cast<std::int64_t>(i);
  };
  const std::int64_t d = key(a) - key(b);
  return d < 0 ? -d : d;
}

const std::vector<double>& AbsErrorBins() {
  static const std::vector<double> bins = [] {
    std::vector<double> edges;
    edges.reserve(483);
    edges.push_back(0.0);
    for (int i = 0; i != 481; ++i) {
      const double t = -9.0 + (12.0 * static_cast<double>(i)) / 480.0;
      edges.push_back(std::pow(10.0, t));
    }
    edges.push_back(std::numeric_limits<double>::infinity());
    return edges;
  }();
  return bins;
}

ErrAcc::ErrAcc() : hist_(AbsErrorBins().size() - 1, 0) {}

void ErrAcc::Add(const std::complex<float>* original, const std::complex<float>* decoded,
                 const std::uint8_t* valid, const std::uint8_t* cross, const float* sigma_bl_pol,
                 std::size_t n_baseline, std::size_t n_channel, std::size_t n_pol) {
  for (std::size_t b = 0; b != n_baseline; ++b) {
    if (cross[b] == 0) continue;
    for (std::size_t f = 0; f != n_channel; ++f) {
      for (std::size_t p = 0; p != n_pol; ++p) {
        const std::size_t i = (b * n_channel + f) * n_pol + p;
        if (valid[i] == 0) continue;
        const std::complex<float> err = decoded[i] - original[i];
        const float parts[2] = {err.real(), err.imag()};
        const float sigma = sigma_bl_pol[b * n_pol + p];
        for (float part : parts) {
          if (!std::isfinite(part)) {
            ++nonfinite_;
            continue;
          }
          ++n_;
          const double component = static_cast<double>(part);
          sse_ += component * component;
          if (!std::isfinite(sigma) || sigma == 0.0f) continue;
          const double normalized = component / static_cast<double>(sigma);
          if (!std::isfinite(normalized)) continue;
          ssn_ += normalized * normalized;
          sn_ += normalized;
          ++nn_;
          const double abs_n = std::fabs(normalized);
          if (abs_n > maxn_) maxn_ = abs_n;
          const std::size_t bin = HistogramBin(abs_n);
          if (bin < hist_.size()) ++hist_[bin];
        }
      }
    }
  }
}

ErrStats ErrAcc::Result() const {
  ErrStats out;
  out.n_cross_components = n_;
  out.n_nonfinite = nonfinite_;
  out.n_normalized = nn_;
  if (nn_ == 0) return out;
  out.rms_over_sigma = std::sqrt(ssn_ / static_cast<double>(nn_));
  out.mean_over_sigma = sn_ / static_cast<double>(nn_);
  out.max_over_sigma = maxn_;
  const std::vector<double>& edges = AbsErrorBins();
  double cumulative = 0.0;
  const double total = static_cast<double>(nn_);
  std::size_t index = hist_.size() - 1;
  for (std::size_t i = 0; i != hist_.size(); ++i) {
    cumulative += static_cast<double>(hist_[i]);
    if (cumulative / total >= 0.999) {
      index = i;
      break;
    }
  }
  out.p999_over_sigma = edges[index + 1];
  const double x = out.rms_over_sigma;
  out.noise_increase_pct = (std::sqrt(1.0 + x * x) - 1.0) * 100.0;
  return out;
}

bool ChiSquare(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b, double* chi2,
               int* degrees_of_freedom) {
  if (a.size() != b.size() || chi2 == nullptr || degrees_of_freedom == nullptr) return false;
  long double n1 = 0.0L;
  long double n2 = 0.0L;
  for (std::size_t i = 0; i != a.size(); ++i) {
    n1 += static_cast<long double>(a[i]);
    n2 += static_cast<long double>(b[i]);
  }
  if (n1 == 0.0L || n2 == 0.0L) return false;
  long double sum = 0.0L;
  int used = 0;
  for (std::size_t i = 0; i != a.size(); ++i) {
    const long double ca = static_cast<long double>(a[i]);
    const long double cb = static_cast<long double>(b[i]);
    if (ca + cb == 0.0L) continue;
    const long double diff = ca / n1 - cb / n2;
    // Pearson two-sample term. (n1+n2) here understates chi-square by about n1*n2/(n1+n2).
    sum += (n1 * n2) * diff * diff / (ca + cb);
    ++used;
  }
  if (used < 2) return false;
  *chi2 = static_cast<double>(sum);
  *degrees_of_freedom = used - 1;
  return true;
}

double ChiSquareP(double chi2, int degrees_of_freedom) {
#if defined(DP3CORE_DYSCO_HAVE_GSL)
  if (degrees_of_freedom < 1 || !std::isfinite(chi2)) return std::numeric_limits<double>::quiet_NaN();
  return gsl_cdf_chisq_Q(chi2, static_cast<double>(degrees_of_freedom));
#else
  (void)chi2;
  (void)degrees_of_freedom;
  return std::numeric_limits<double>::quiet_NaN();
#endif
}

void SlotChannelSigma(const std::complex<float>* data, const std::uint8_t* valid, std::size_t n_baseline,
                      std::size_t n_channel, std::size_t n_pol, float* sigma_bl_pol) {
  std::vector<float> diffs;
  diffs.reserve(n_channel * 2);
  for (std::size_t b = 0; b != n_baseline; ++b) {
    for (std::size_t p = 0; p != n_pol; ++p) {
      diffs.clear();
      for (std::size_t c = 0; c + 1 < n_channel; ++c) {
        const std::size_t i0 = (b * n_channel + c) * n_pol + p;
        const std::size_t i1 = i0 + n_pol;
        if (valid[i0] == 0 || valid[i1] == 0) continue;
        const std::complex<float> d = data[i1] - data[i0];
        if (std::isfinite(d.real())) diffs.push_back(d.real());
        if (std::isfinite(d.imag())) diffs.push_back(d.imag());
      }
      float sigma = std::numeric_limits<float>::quiet_NaN();
      if (diffs.size() >= kMinDifferences) {
        const double center = MedianInPlace(diffs);
        for (float& value : diffs) value = static_cast<float>(std::fabs(static_cast<double>(value) - center));
        const double mad = MedianInPlace(diffs);
        sigma = static_cast<float>(kMadToSigma * mad / std::sqrt(2.0));
      }
      sigma_bl_pol[b * n_pol + p] = sigma;
    }
  }
}

std::vector<float> ChannelDiffSigma(const std::vector<std::vector<float>>& per_slot_sigma, std::size_t n_pol) {
  if (per_slot_sigma.empty() || n_pol == 0 || per_slot_sigma[0].size() % n_pol != 0)
    return {};
  const std::size_t n_baseline = per_slot_sigma[0].size() / n_pol;
  std::vector<float> out(n_baseline * n_pol, std::numeric_limits<float>::quiet_NaN());
  std::vector<float> values;
  values.reserve(per_slot_sigma.size());
  for (std::size_t b = 0; b != n_baseline; ++b) {
    for (std::size_t p = 0; p != n_pol; ++p) {
      values.clear();
      for (const auto& slot : per_slot_sigma) {
        const float s = slot[b * n_pol + p];
        if (std::isfinite(s)) values.push_back(s);
      }
      if (!values.empty()) out[b * n_pol + p] = static_cast<float>(MedianInPlace(values));
    }
  }
  return out;
}

}  // namespace dp3core::dysco::harness
