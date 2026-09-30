// Ports of Dysco's rowtimeblockencoder.cc, rftimeblockencoder.cc and
// aftimeblockencoder.cc (aroffringa/dysco @ aa30453, GPL-3.0). Loop order,
// operation order and float/double conversions are kept exactly, so that
// side data and normalized values are bit-identical to Dysco's.
// SPDX-License-Identifier: GPL-3.0-only
#include "normalize.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace dp3core::dysco::detail {
namespace {

using cd = std::complex<double>;

// Dysco's RMSMeasurement (timeblockencoder.h).
class Rms {
 public:
  void Include(const cd& v) {
    if (std::isfinite(v.real()) && std::isfinite(v.imag())) {
      ++count_;
      value_ += v.real() * v.real() + v.imag() * v.imag();
    }
  }
  // 0/0 = NaN when nothing was included, as in Dysco.
  double Value() const {
    return std::sqrt(value_ / static_cast<double>(count_ * 2));
  }

 private:
  std::size_t count_ = 0;
  double value_ = 0.0;
};

// max(max(re, im), -min(re, im)) with Dysco's argument order (NaN handling).
double LargestComponent(const cd& v) {
  return std::max(std::max(v.real(), v.imag()), -std::min(v.real(), v.imag()));
}

}  // namespace

void NormalizeRow(cd* data, const BlockLayout& l, double max_level,
                  float* side) {
  const std::size_t vis_per_row = l.n_pol * l.n_channel;
  for (std::size_t r = 0; r != l.n_baseline; ++r) {
    cd* row = data + r * vis_per_row;
    double max_val = 0.0;
    for (std::size_t i = 0; i != vis_per_row; ++i) {
      const double m = std::max(std::fabs(row[i].real()), std::fabs(row[i].imag()));
      if (std::isfinite(m)) max_val = std::max(max_val, m);
    }
    const double factor = (max_val == 0.0) ? 1.0 : max_level / max_val;
    for (std::size_t i = 0; i != vis_per_row; ++i) row[i] *= factor;
    side[r] = static_cast<float>(max_val / max_level);
  }
}

void NormalizeRF(cd* data, const BlockLayout& l, double max_level,
                 float* side) {
  const std::size_t vis_per_row = l.n_pol * l.n_channel;
  // maximizeRows
  for (std::size_t r = 0; r != l.n_baseline; ++r) {
    cd* row = data + r * vis_per_row;
    for (std::size_t p = 0; p != l.n_pol; ++p) {
      double max_val = 0.0;
      for (std::size_t ch = 0; ch != l.n_channel; ++ch) {
        const cd v = row[ch * l.n_pol + p];
        const double m = std::max(std::fabs(v.real()), std::fabs(v.imag()));
        if (std::isfinite(m)) max_val = std::max(max_val, m);
      }
      const double factor = max_val == 0.0 ? 1.0 : max_level / max_val;
      for (std::size_t ch = 0; ch != l.n_channel; ++ch)
        row[ch * l.n_pol + p] *= factor;
      side[vis_per_row + r * l.n_pol + p] =
          static_cast<float>((max_level == 0.0) ? 1.0 : max_val / max_level);
    }
  }
  // maximizeChannels
  for (std::size_t vi = 0; vi != vis_per_row; ++vi) {
    double largest = 0.0;
    for (std::size_t r = 0; r != l.n_baseline; ++r) {
      const cd& v = data[r * vis_per_row + vi];
      const double local_max = std::max(std::fabs(v.real()), std::fabs(v.imag()));
      if (std::isfinite(local_max) && local_max > largest) largest = local_max;
    }
    const double factor =
        (max_level == 0.0 || largest == 0.0) ? 1.0 : max_level / largest;
    side[vi] = static_cast<float>(1.0 / factor);
    for (std::size_t r = 0; r != l.n_baseline; ++r) data[r * vis_per_row + vi] *= factor;
  }
}

namespace {

class AfEncoder {
 public:
  AfEncoder(cd* data, const BlockLayout& l, float* side)
      : data_(data), l_(l), side_(side), vis_per_row_(l.n_pol * l.n_channel),
        rms_per_antenna_(l.n_antenna, 0.0) {}

  cd& At(std::size_t r, std::size_t i) { return data_[r * vis_per_row_ + i]; }
  bool IsCross(std::size_t r) const { return l_.antenna1[r] != l_.antenna2[r]; }

  void Encode(double max_level) {
    const std::size_t n_ant = l_.n_antenna;
    // Channel RMS.
    std::vector<Rms> channel_rms(vis_per_row_);
    for (std::size_t r = 0; r != l_.n_baseline; ++r)
      for (std::size_t i = 0; i != vis_per_row_; ++i) channel_rms[i].Include(At(r, i));
    for (std::size_t r = 0; r != l_.n_baseline; ++r) {
      for (std::size_t i = 0; i != vis_per_row_; ++i) {
        const double rms = channel_rms[i].Value();
        if (rms != 0.0) At(r, i) /= rms;
        side_[i] = static_cast<float>(rms);
      }
    }
    // Antenna RMS per polarization.
    for (std::size_t p = 0; p != l_.n_pol; ++p) {
      CalculateAntennaRms(p);
      for (std::size_t r = 0; r != l_.n_baseline; ++r) {
        const double mul = rms_per_antenna_[l_.antenna1[r]] * rms_per_antenna_[l_.antenna2[r]];
        const double fac = (mul == 0.0) ? 0.0 : 1.0 / mul;
        for (std::size_t ch = 0; ch != l_.n_channel; ++ch) At(r, ch * l_.n_pol + p) *= fac;
      }
      const std::size_t meta_index = vis_per_row_ + n_ant * p;
      for (std::size_t a = 0; a != n_ant; ++a)
        side_[meta_index + a] = static_cast<float>(rms_per_antenna_[a]);
    }
    FitToMaximum(max_level);
  }

 private:
  void CalculateAntennaRms(std::size_t p) {
    const std::size_t n_ant = l_.n_antenna;
    std::vector<Rms> matrix_meas(n_ant * n_ant);
    for (std::size_t r = 0; r != l_.n_baseline; ++r) {
      std::size_t a1 = l_.antenna1[r], a2 = l_.antenna2[r];
      if (a1 != a2) {
        if (a1 > a2) std::swap(a1, a2);
        const std::size_t index = a1 * n_ant + a2;
        for (std::size_t ch = 0; ch != l_.n_channel; ++ch)
          matrix_meas[index].Include(At(r, ch * l_.n_pol + p));
      }
    }
    std::vector<double> matrix(matrix_meas.size());
    for (std::size_t i = 0; i != n_ant; ++i) {
      for (std::size_t j = i; j != n_ant; ++j) {
        matrix[i * n_ant + j] = matrix_meas[i * n_ant + j].Value();
        matrix[j * n_ant + i] = matrix_meas[i * n_ant + j].Value();
      }
    }
    for (std::size_t i = 0; i != n_ant; ++i) rms_per_antenna_[i] = 1.0;

    double precision = 1.0;
    for (std::size_t iteration = 0; iteration != 100 && precision > 1e-6; ++iteration) {
      std::vector<double> next(n_ant, 0.0);
      for (std::size_t i = 0; i != n_ant; ++i) {
        double weight_sum = 0.0;
        for (std::size_t j = 0; j != n_ant; ++j) {
          if (i != j && rms_per_antenna_[j] != 0.0) {
            const double w = rms_per_antenna_[j];
            if (std::isfinite(matrix[i * n_ant + j])) {
              next[i] += matrix[i * n_ant + j];
              weight_sum += w;
            }
          }
        }
        if (weight_sum == 0.0)
          next[i] = 0.0;
        else
          next[i] /= weight_sum;
      }
      double max_val = 0.0;
      for (std::size_t i = 0; i != n_ant; ++i) {
        rms_per_antenna_[i] = next[i] * 0.8 + rms_per_antenna_[i] * 0.2;
        max_val = std::max(rms_per_antenna_[i], max_val);
      }
      precision = 0.0;
      for (std::size_t i = 0; i != n_ant; ++i) {
        if (rms_per_antenna_[i] < max_val * 1e-5) rms_per_antenna_[i] = 0.0;
        precision = std::max(precision, std::fabs(rms_per_antenna_[i] - next[i]) / max_val);
      }
    }
  }

  void ChangeChannelFactor(std::size_t vi, double factor) {
    side_[vi] = static_cast<float>(static_cast<double>(side_[vi]) / factor);
    for (std::size_t r = 0; r != l_.n_baseline; ++r) At(r, vi) *= factor;
  }

  void ChangeAntennaFactor(std::size_t antenna, std::size_t p, double factor) {
    const std::size_t meta_index = vis_per_row_ + l_.n_antenna * p;
    side_[meta_index + antenna] =
        static_cast<float>(static_cast<double>(side_[meta_index + antenna]) / factor);
    for (std::size_t r = 0; r != l_.n_baseline; ++r) {
      unsigned count = 0;
      if (l_.antenna1[r] == antenna) ++count;
      if (l_.antenna2[r] == antenna) ++count;
      for (unsigned repeat = 0; repeat != count; ++repeat)
        for (std::size_t i = 0; i != l_.n_channel; ++i) At(r, i * l_.n_pol + p) *= factor;
    }
  }

  void FitToMaximum(double max_level) {
    const std::size_t n_ant = l_.n_antenna;
    for (std::size_t vi = 0; vi != vis_per_row_; ++vi) {
      double largest = 0.0;
      for (std::size_t r = 0; r != l_.n_baseline; ++r) {
        if (IsCross(r)) {
          const double local_max = LargestComponent(At(r, vi));
          if (std::isfinite(local_max) && local_max > largest) largest = local_max;
        }
      }
      const double factor =
          (max_level == 0.0 || largest == 0.0) ? 1.0 : max_level / largest;
      ChangeChannelFactor(vi, factor);
    }

    for (std::size_t p = 0; p != l_.n_pol; ++p) {
      bool is_progressing;
      do {
        double best_channel_increase = 0.0, channel_factor = 1.0;
        std::size_t best_channel = 0;
        for (std::size_t ch = 0; ch != l_.n_channel; ++ch) {
          double largest = 0.0;
          for (std::size_t r = 0; r != l_.n_baseline; ++r) {
            if (IsCross(r)) {
              const double local_max = LargestComponent(At(r, ch * l_.n_pol + p));
              if (std::isfinite(local_max) && local_max > largest) largest = local_max;
            }
          }
          const double factor = (largest == 0.0) ? 0.0 : (max_level / largest - 1.0);
          double increase = 0.0;
          for (std::size_t r = 0; r != l_.n_baseline; ++r) {
            if (IsCross(r)) {
              const cd v = At(r, ch * l_.n_pol + p) * factor;
              const double abs_value = std::fabs(v.real()) + std::fabs(v.imag());
              if (std::isfinite(abs_value)) increase += abs_value;
            }
          }
          if (increase > best_channel_increase) {
            best_channel_increase = increase;
            best_channel = ch;
            channel_factor = factor + 1.0;
          }
        }

        std::vector<double> max_comp(n_ant, 0.0);
        for (std::size_t r = 0; r != l_.n_baseline; ++r) {
          if (IsCross(r)) {
            for (std::size_t ch = 0; ch != l_.n_channel; ++ch) {
              const double compl_max = LargestComponent(At(r, ch * l_.n_pol + p));
              if (std::isfinite(compl_max)) {
                if (compl_max > max_comp[l_.antenna1[r]]) max_comp[l_.antenna1[r]] = compl_max;
                if (compl_max > max_comp[l_.antenna2[r]]) max_comp[l_.antenna2[r]] = compl_max;
              }
            }
          }
        }
        std::vector<double> increase_per_antenna(n_ant, 0.0);
        for (std::size_t r = 0; r != l_.n_baseline; ++r) {
          if (IsCross(r)) {
            const std::size_t a1 = l_.antenna1[r], a2 = l_.antenna2[r];
            const double f1 = (max_comp[a1] == 0.0) ? 0.0 : (max_level / max_comp[a1] - 1.0);
            const double f2 = (max_comp[a2] == 0.0) ? 0.0 : (max_level / max_comp[a2] - 1.0);
            for (std::size_t ch = 0; ch != l_.n_channel; ++ch) {
              const cd v1 = At(r, ch * l_.n_pol + p) * f1;
              const double av1 = std::fabs(v1.real()) + std::fabs(v1.imag());
              if (std::isfinite(av1)) increase_per_antenna[a1] += av1;
              const cd v2 = At(r, ch * l_.n_pol + p) * f2;
              const double av2 = std::fabs(v2.real()) + std::fabs(v2.imag());
              if (std::isfinite(av2)) increase_per_antenna[a2] += av2;
            }
          }
        }
        std::size_t best_antenna = 0;
        double best_antenna_increase = 0.0;
        for (std::size_t a = 0; a != n_ant; ++a) {
          if (increase_per_antenna[a] > best_antenna_increase) {
            best_antenna_increase = increase_per_antenna[a];
            best_antenna = a;
          }
        }
        if (best_antenna_increase > best_channel_increase) {
          const double factor = (max_comp[best_antenna] == 0.0)
                                    ? 1.0
                                    : (max_level / max_comp[best_antenna]);
          if (factor < 1.0) {
            is_progressing = false;
          } else {
            is_progressing = factor > 1.01;
            ChangeAntennaFactor(best_antenna, p, factor);
          }
        } else {
          if (channel_factor < 1.0) {
            is_progressing = false;
          } else {
            is_progressing = channel_factor > 1.001;
            ChangeChannelFactor(best_channel * l_.n_pol + p, channel_factor);
          }
        }
      } while (is_progressing);
    }
  }

  cd* data_;
  const BlockLayout& l_;
  float* side_;
  std::size_t vis_per_row_;
  std::vector<double> rms_per_antenna_;
};

}  // namespace

void NormalizeAF(cd* data, const BlockLayout& l, double max_level, float* side) {
  AfEncoder(data, l, side).Encode(max_level);
}

void DenormalizeRow(const std::uint32_t* symbols, const float* levels,
                    const BlockLayout& l, const float* side,
                    std::complex<float>* out) {
  const std::size_t vis_per_row = l.n_pol * l.n_channel;
  for (std::size_t r = 0; r != l.n_baseline; ++r) {
    for (std::size_t i = 0; i != vis_per_row; ++i) {
      const double factor = side[r];
      out->real(static_cast<float>(double(levels[*symbols]) * factor));
      ++symbols;
      out->imag(static_cast<float>(double(levels[*symbols]) * factor));
      ++symbols;
      ++out;
    }
  }
}

void DenormalizeRF(const std::uint32_t* symbols, const float* levels,
                   const BlockLayout& l, const float* side,
                   std::complex<float>* out) {
  const std::size_t vis_per_row = l.n_pol * l.n_channel;
  const float* row_factors = side + vis_per_row;
  for (std::size_t r = 0; r != l.n_baseline; ++r) {
    for (std::size_t i = 0; i != vis_per_row; ++i) {
      const double ch_factor = side[i];
      const double factor = ch_factor * double(row_factors[r * l.n_pol + i % l.n_pol]);
      out->real(static_cast<float>(double(levels[*symbols]) * factor));
      ++symbols;
      out->imag(static_cast<float>(double(levels[*symbols]) * factor));
      ++symbols;
      ++out;
    }
  }
}

void DenormalizeAF(const std::uint32_t* symbols, const float* levels,
                   const BlockLayout& l, const float* side,
                   std::complex<float>* out) {
  const std::size_t vis_per_row = l.n_pol * l.n_channel;
  const float* ant = side + vis_per_row;
  std::vector<double> ant_factors(l.n_pol);
  for (std::size_t r = 0; r != l.n_baseline; ++r) {
    for (std::size_t p = 0; p != l.n_pol; ++p)
      ant_factors[p] = double(ant[l.antenna1[r] + p * l.n_antenna]) *
                       double(ant[l.antenna2[r] + p * l.n_antenna]);
    for (std::size_t ch = 0; ch != l.n_channel; ++ch) {
      for (std::size_t p = 0; p != l.n_pol; ++p) {
        const double ch_rms = side[ch * l.n_pol + p];
        const double factor = ch_rms * ant_factors[p];
        out->real(static_cast<float>(double(levels[*symbols]) * factor));
        ++symbols;
        out->imag(static_cast<float>(double(levels[*symbols]) * factor));
        ++symbols;
        ++out;
      }
    }
  }
}

}  // namespace dp3core::dysco::detail
