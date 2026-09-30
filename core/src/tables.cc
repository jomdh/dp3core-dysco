// Portable quantization tables. See spec/SPEC.md section 4 for the normative
// algorithm; every constant and loop bound here is part of the format.
// SPDX-License-Identifier: GPL-3.0-only
//
// Ported behaviour: aroffringa/dysco stochasticencoder.cc (Gaussian and uniform
// levels scaled by float(sqrt(3)); truncated-Gaussian levels from the CDF
// window [Phi(-k), 1 - Phi(-k)]). The numerics are new: the original uses GSL
// and long double, which differ between platforms.
#include "dp3core/dysco/tables.h"

#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>
#include <stdexcept>
#include <string>

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

namespace dp3core::dysco {

const char* ToString(Distribution d) {
  switch (d) {
    case Distribution::kGaussian:
      return "gaussian";
    case Distribution::kTruncatedGaussian:
      return "truncated_gaussian";
    case Distribution::kUniform:
      return "uniform";
  }
  return "invalid";
}

const char* ToString(Normalization n) {
  switch (n) {
    case Normalization::kAF:
      return "af";
    case Normalization::kRF:
      return "rf";
    case Normalization::kRow:
      return "row";
  }
  return "invalid";
}

const char* ToString(Kind k) {
  switch (k) {
    case Kind::kData:
      return "data";
    case Kind::kWeights:
      return "weights";
  }
  return "invalid";
}

namespace portable {
namespace {
// Decimal literals; each is converted to the nearest double by the compiler.
constexpr double kLn2Hi = 6.93147180369123816490e-01;  // high part, 32 zero low bits
constexpr double kLn2Lo = 1.90821492927058770002e-10;
constexpr double kInvLn2 = 1.44269504088896338700e+00;
constexpr double kInvSqrtPi = 5.64189583547756286948e-01;
constexpr double kInvSqrt2 = 7.07106781186547524401e-01;

constexpr int kExpTaylorTerms = 27;
constexpr double kErfcSeriesLimit = 1.0;
constexpr int kErfSeriesTerms = 40;
// Continued-fraction depth by range of z (deterministic, see SPEC.md 4.2).
constexpr int FractionDepth(double z) { return z < 2.0 ? 250 : (z < 4.0 ? 80 : 30); }
constexpr int kBisectionSteps = 64;
constexpr double kBisectionLow = -16.0;
}  // namespace

double Exp(double x) {
  if (!(x <= 0.0)) throw std::invalid_argument("portable::Exp: x must be <= 0");
  if (x < -745.0) return 0.0;
  // Round-half-away-from-zero of x / ln 2, written without libm.
  const double k = x * kInvLn2;
  const long n = -static_cast<long>(-k + 0.5);
  const double nd = static_cast<double>(n);
  const double r = (x - nd * kLn2Hi) - nd * kLn2Lo;  // |r| <= ~0.347
  double p = 1.0;
  for (int i = kExpTaylorTerms; i >= 1; --i) p = 1.0 + r / i * p;
  return std::ldexp(p, static_cast<int>(n));  // exact scaling by 2^n
}

double Erfc(double z) {
  if (!(z >= 0.0)) throw std::invalid_argument("portable::Erfc: z must be >= 0");
  const double z2 = z * z;
  if (z < kErfcSeriesLimit) {
    // erf(z) = 2/sqrt(pi) e^{-z^2} sum_n (2z^2)^n z / (1*3*...*(2n+1))
    double term = z;
    double sum = z;
    for (int n = 1; n < kErfSeriesTerms; ++n) {
      term = term * (2.0 * z2) / (2.0 * n + 1.0);
      sum += term;
    }
    return 1.0 - 2.0 * kInvSqrtPi * Exp(-z2) * sum;
  }
  // erfc(z) = e^{-z^2}/sqrt(pi) / (z + (1/2)/(z + 1/(z + (3/2)/(z + ...)))),
  // evaluated from the inside out with a fixed depth.
  double t = z;
  for (int n = FractionDepth(z); n >= 1; --n) t = z + (0.5 * n) / t;
  return kInvSqrtPi * Exp(-z2) / t;
}

double PhiLower(double x) {
  if (!(x <= 0.0)) throw std::invalid_argument("portable::PhiLower: x must be <= 0");
  return 0.5 * Erfc(-x * kInvSqrt2);
}

double InversePhiLower(double p) {
  if (!(p > 0.0 && p <= 0.5))
    throw std::invalid_argument("portable::InversePhiLower: p must be in (0, 0.5]");
  if (p == 0.5) return 0.0;
  double low = kBisectionLow;
  double high = 0.0;
  for (int i = 0; i < kBisectionSteps; ++i) {
    const double mid = 0.5 * (low + high);
    if (PhiLower(mid) < p)
      low = mid;
    else
      high = mid;
  }
  return 0.5 * (low + high);
}

}  // namespace portable

std::vector<float> DecodeTable(unsigned bits, Distribution distribution,
                               double truncation) {
  if (!IsSupportedBitCount(bits))
    throw std::invalid_argument("unsupported bit count " + std::to_string(bits) +
                                " (use 2, 3, 4, 6, 8, 10, 12 or 16)");
  // n levels, n odd; the middle level m is exactly 0.
  const unsigned long n = (1ul << bits) - 1ul;
  const unsigned long m = (n - 1) / 2;
  // float(sqrt(3)) as in Dysco, where the scale is held in a float.
  const double sqrt3_float = static_cast<double>(1.7320508075688772f);
  std::vector<float> table(n);

  double tail = 0.0;   // Phi(-k)
  double width = 0.0;  // 1 - 2 Phi(-k)
  if (distribution == Distribution::kTruncatedGaussian) {
    if (!(truncation > 0.0 && truncation <= kMaxTruncation))
      throw std::invalid_argument("truncation must be in (0, 8], got " +
                                  std::to_string(truncation));
    tail = portable::PhiLower(-truncation);
    width = 1.0 - 2.0 * tail;
  } else if (distribution != Distribution::kGaussian &&
             distribution != Distribution::kUniform) {
    throw std::invalid_argument("invalid distribution code");
  }

  for (unsigned long i = 0; i < m; ++i) {
    const double numerator = static_cast<double>(2 * i + 1);    // exact
    const double denominator = static_cast<double>(2 * n);      // exact
    double level = 0.0;
    switch (distribution) {
      case Distribution::kGaussian:
        level = sqrt3_float * portable::InversePhiLower(numerator / denominator);
        break;
      case Distribution::kTruncatedGaussian:
        level = portable::InversePhiLower(tail + (numerator / denominator) * width);
        break;
      case Distribution::kUniform:
        // -1 + (2i+1)/n, written as one exact-numerator division.
        level = sqrt3_float *
                (static_cast<double>(2 * i + 1) - static_cast<double>(n)) /
                static_cast<double>(n);
        break;
    }
    const float value = static_cast<float>(level);
    table[i] = value;
    table[n - 1 - i] = -value;
  }
  table[m] = 0.0f;
  return table;
}

std::uint64_t TableChecksum(const std::vector<float>& table) {
  std::vector<unsigned char> bytes(table.size() * 4);
  for (std::size_t i = 0; i != table.size(); ++i) {
    std::uint32_t u;
    std::memcpy(&u, &table[i], 4);
    for (int b = 0; b != 4; ++b)
      bytes[4 * i + static_cast<std::size_t>(b)] =
          static_cast<unsigned char>((u >> (8 * b)) & 0xFFu);
  }
  return XXH3_64bits(bytes.data(), bytes.size());
}

std::shared_ptr<const CachedTable> GetCachedTable(unsigned bits,
                                                  Distribution distribution,
                                                  double truncation) {
  if (distribution != Distribution::kTruncatedGaussian) truncation = 0.0;
  std::uint64_t truncation_bits;
  std::memcpy(&truncation_bits, &truncation, 8);
  using Key = std::tuple<unsigned, int, std::uint64_t>;
  static std::mutex mutex;
  static std::map<Key, std::shared_ptr<const CachedTable>> cache;
  const Key key{bits, static_cast<int>(distribution), truncation_bits};
  {
    const std::lock_guard<std::mutex> lock(mutex);
    const auto it = cache.find(key);
    if (it != cache.end()) return it->second;
  }
  // Built outside the lock; a concurrent duplicate build is harmless.
  auto table = std::make_shared<CachedTable>();
  table->levels = DecodeTable(bits, distribution, truncation);
  table->checksum = TableChecksum(table->levels);
  const std::lock_guard<std::mutex> lock(mutex);
  return cache.emplace(key, std::move(table)).first->second;
}

}  // namespace dp3core::dysco
