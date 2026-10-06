// Error statistics and the symbol-histogram chi-square used by the harness.
// SPDX-License-Identifier: GPL-3.0-only
//
// ErrAcc and the bin edges follow bench/compression/src/bench.py so a number
// from this harness is the same quantity as a COMPRESSION-BENCH number.
#ifndef DP3CORE_DYSCO_HARNESS_STATS_H_
#define DP3CORE_DYSCO_HARNESS_STATS_H_

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dp3core::dysco::harness {

/// Distance in units in the last place between two finite floats.
std::int64_t UlpDistance(float a, float b);

/// Right-hand edges of the |error/σ| histogram, including +inf as the last edge.
/// Size is 483: 0, 481 logarithmically spaced edges, +inf.
const std::vector<double>& AbsErrorBins();

struct ErrStats {
  double rms_over_sigma = 0.0;
  double mean_over_sigma = 0.0;
  double max_over_sigma = 0.0;
  double p999_over_sigma = 0.0;
  double noise_increase_pct = 0.0;
  std::uint64_t n_cross_components = 0;
  std::uint64_t n_nonfinite = 0;
  std::uint64_t n_normalized = 0;
};

/// Accumulates unflagged cross-correlation component errors in units of σ.
/// σ is one value per (baseline, polarization) and is broadcast across channels.
class ErrAcc {
 public:
  ErrAcc();
  /// @p original and @p decoded are C-order (baseline, channel, pol) for one
  /// time slot. @p valid is the same shape, true where the sample counts.
  /// @p cross is one flag per baseline. @p sigma_bl_pol is (n_baseline, n_pol),
  /// row-major; a non-finite σ drops that component from the normalized stats.
  void Add(const std::complex<float>* original, const std::complex<float>* decoded,
           const std::uint8_t* valid, const std::uint8_t* cross, const float* sigma_bl_pol,
           std::size_t n_baseline, std::size_t n_channel, std::size_t n_pol);
  ErrStats Result() const;

 private:
  std::uint64_t n_ = 0;
  double sse_ = 0.0;
  double ssn_ = 0.0;
  double sn_ = 0.0;
  double maxn_ = 0.0;
  std::uint64_t nn_ = 0;
  std::uint64_t nonfinite_ = 0;
  std::vector<std::uint64_t> hist_;
};

/// Two-sample χ² of two symbol histograms. Bins where both counts are 0 are
/// skipped. Returns false when fewer than 2 bins were used.
bool ChiSquare(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b,
               double* chi2, int* degrees_of_freedom);

/// Upper-tail p-value of a χ² variate. Returns a quiet NaN when GSL was not
/// linked or when @p degrees_of_freedom < 1.
double ChiSquareP(double chi2, int degrees_of_freedom);

/// Channel-difference MAD σ per (baseline, polarization), divided by sqrt(2).
/// Matches bench.py `estimate_noise`: median over time slots of each baseline,
/// and a slot with fewer than 20 finite differences contributes a NaN.
std::vector<float> ChannelDiffSigma(const std::vector<std::vector<float>>& per_slot_sigma,
                                    std::size_t n_pol);

/// Per-polarization channel-difference σ for one time slot.
/// @p valid is 0/1, same shape as the complex samples (baseline, channel, pol).
void SlotChannelSigma(const std::complex<float>* data, const std::uint8_t* valid,
                      std::size_t n_baseline, std::size_t n_channel, std::size_t n_pol,
                      float* sigma_bl_pol);

}  // namespace dp3core::dysco::harness

#endif
