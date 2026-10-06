// Casacore Dysco equivalence harness (codec design section 9.3).
// SPDX-License-Identifier: GPL-3.0-only
//
// Usage:
//   dp3core_dysco_p2 --input IN.MS --casacore A.MS B.MS ... --out DIR [--caveat TEXT]
// Exit 0: every check passed (PASS_EXPLAIN counts as passed).
// Exit 1: a check failed or was not evaluated. The report is still written.
// Exit 2: usage or I/O error.
#include "dysco_file.h"
#include "gsl_table.h"
#include "stats.h"

#include <casacore/casa/Arrays/Array.h>
#include <casacore/casa/Arrays/Slicer.h>
#include <casacore/casa/Containers/Record.h>
#include <casacore/tables/Tables/ArrayColumn.h>
#include <casacore/tables/Tables/ScalarColumn.h>
#include <casacore/tables/Tables/Table.h>
#include <casacore/tables/Tables/TableLock.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "dp3core/dysco/bitpack.h"
#include "dp3core/dysco/codec.h"
#include "dp3core/dysco/header.h"
#include "dp3core/dysco/tables.h"
#include "dp3core/dysco/version.h"

namespace {

using dp3core::dysco::AntennaMap;
using dp3core::dysco::BlockBytes;
using dp3core::dysco::ChunkHeader;
using dp3core::dysco::DataParams;
using dp3core::dysco::DecodeData;
using dp3core::dysco::DecodeWeights;
using dp3core::dysco::Distribution;
using dp3core::dysco::EncodeData;
using dp3core::dysco::EncodeWeights;
using dp3core::dysco::EncodedSize;
using dp3core::dysco::Kind;
using dp3core::dysco::LibraryVersion;
using dp3core::dysco::MakeDataHeader;
using dp3core::dysco::MakeWeightsHeader;
using dp3core::dysco::Normalization;
using dp3core::dysco::PackSymbols;
using dp3core::dysco::PackedBytes;
using dp3core::dysco::Shape;
using dp3core::dysco::TableChecksum;
using dp3core::dysco::UnpackSymbols;
using dp3core::dysco::WeightParams;
using dp3core::dysco::WriteHeader;
using dp3core::dysco::kFlagDictionaryEmbedded;
using dp3core::dysco::harness::AbsErrorBins;
using dp3core::dysco::harness::ChannelDiffSigma;
using dp3core::dysco::harness::ChiSquare;
using dp3core::dysco::harness::ChiSquareP;
using dp3core::dysco::harness::DyscoFile;
using dp3core::dysco::harness::ErrAcc;
using dp3core::dysco::harness::ErrStats;
using dp3core::dysco::harness::GslAvailable;
using dp3core::dysco::harness::GslDecodeTable;
using dp3core::dysco::harness::SlotChannelSigma;
using dp3core::dysco::harness::UlpDistance;

constexpr const char* kExplain =
    "relative difference at most 1e-6; casacore Dysco may contract fused multiply-adds, "
    "this library is built with -ffp-contract=off";

struct Check {
  std::string name;
  std::string status;
  std::string detail;
  std::vector<std::pair<std::string, double>> numbers;
};

struct FloatDiff {
  std::int64_t max_ulp = 0;
  std::uint64_t nan_mismatch = 0;
  std::uint64_t nan_payload = 0;
  std::uint64_t n = 0;
};

struct SideDiff {
  std::uint64_t n = 0;
  std::uint64_t n_diff = 0;
  double max_rel = 0.0;
};

struct Options {
  std::string input;
  std::vector<std::string> casacore;
  std::string out;
  std::string caveat;
};

[[noreturn]] void Usage() {
  std::cerr << "usage: dp3core_dysco_p2 --input IN.MS --casacore A.MS [B.MS ...] "
               "--out DIR [--caveat TEXT]\n";
  throw std::runtime_error("usage");
}

Options Parse(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto need = [&]() {
      if (i + 1 >= argc) Usage();
      return std::string(argv[++i]);
    };
    if (a == "--input")
      opt.input = need();
    else if (a == "--out")
      opt.out = need();
    else if (a == "--caveat")
      opt.caveat = need();
    else if (a == "--casacore") {
      if (i + 1 >= argc) Usage();
      while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0)
        opt.casacore.emplace_back(argv[++i]);
    } else {
      Usage();
    }
  }
  if (opt.input.empty() || opt.out.empty() || opt.casacore.empty()) Usage();
  return opt;
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\\' || c == '"') o.push_back('\\');
    if (c == '\n') {
      o += "\\n";
      continue;
    }
    o.push_back(c);
  }
  return o;
}

std::string Num(double v) {
  if (!std::isfinite(v)) return std::isnan(v) ? "null" : (v > 0 ? "1e999" : "-1e999");
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.17g", v);
  return buf;
}

Distribution MapDistribution(std::uint8_t raw) {
  // Dysco's enum: 0 Gaussian, 1 Uniform, 2 Student's T, 3 TruncatedGaussian.
  switch (raw) {
    case 0: return Distribution::kGaussian;
    case 1: return Distribution::kUniform;
    case 3: return Distribution::kTruncatedGaussian;
    default:
      throw std::runtime_error("Dysco distribution code " + std::to_string(raw) +
                              " is not implemented by this codec");
  }
}

Normalization MapNormalization(std::uint8_t raw) {
  if (raw > 2) throw std::runtime_error("Dysco normalization code " + std::to_string(raw));
  return static_cast<Normalization>(raw);
}

struct ColRef {
  std::string path;
  std::uint32_t index = 0;
};

struct RunCols {
  ColRef data;
  ColRef weight;
  bool have_weight = false;
  DyscoFile data_file;
  DyscoFile weight_file;
};

RunCols FindDysco(const std::string& ms) {
  casacore::Table tab(ms, casacore::TableLock(casacore::TableLock::AutoNoReadLocking),
                      casacore::Table::Old);
  casacore::Record info = tab.dataManagerInfo();
  RunCols cols;
  bool have_data = false;
  for (casacore::uInt i = 0; i < info.nfields(); ++i) {
    const casacore::Record& rec = info.subRecord(i);
    if (rec.asString("TYPE") != "DyscoStMan") continue;
    const std::string path = ms + "/table.f" + std::to_string(rec.asInt("SEQNR"));
    casacore::Vector<casacore::String> names = rec.asArrayString("COLUMNS");
    for (casacore::uInt c = 0; c < names.nelements(); ++c) {
      const std::string name = names[c];
      ColRef ref;
      ref.path = path;
      ref.index = static_cast<std::uint32_t>(c);
      if (name == "DATA") {
        cols.data = ref;
        have_data = true;
      } else if (name == "WEIGHT_SPECTRUM") {
        cols.weight = ref;
        cols.have_weight = true;
      }
    }
  }
  if (!have_data) throw std::runtime_error("no Dysco DATA column in " + ms);
  cols.data_file = DyscoFile(cols.data.path);
  if (cols.data.index >= cols.data_file.header().columns.size())
    throw std::runtime_error("DATA column index past the Dysco file header in " + ms);
  if (cols.have_weight) {
    cols.weight_file = DyscoFile(cols.weight.path);
    if (cols.weight.index >= cols.weight_file.header().columns.size())
      throw std::runtime_error("WEIGHT_SPECTRUM column index past the Dysco file header in " + ms);
  }
  return cols;
}

template <typename T>
std::vector<T> ReadSlice(casacore::ArrayColumn<T>& col, std::size_t row0, std::size_t nrow) {
  static_assert(!std::is_same<T, bool>::value, "casacore::Bool is bool; use ReadFlags");
  casacore::Array<T> arr;
  col.getColumnRange(casacore::Slicer(casacore::IPosition(1, static_cast<ssize_t>(row0)),
                                      casacore::IPosition(1, static_cast<ssize_t>(nrow))),
                     arr, casacore::True);
  bool del = false;
  const T* p = arr.getStorage(del);
  std::vector<T> out(static_cast<std::size_t>(arr.nelements()));
  if (!out.empty()) std::memcpy(out.data(), p, out.size() * sizeof(T));
  arr.freeStorage(p, del);
  return out;
}

// casacore::Bool is bool, and std::vector<bool> has no data(). Store 0 or 1.
std::vector<std::uint8_t> ReadFlags(casacore::ArrayColumn<casacore::Bool>& col, std::size_t row0,
                                    std::size_t nrow) {
  casacore::Array<casacore::Bool> arr;
  col.getColumnRange(casacore::Slicer(casacore::IPosition(1, static_cast<ssize_t>(row0)),
                                      casacore::IPosition(1, static_cast<ssize_t>(nrow))),
                     arr, casacore::True);
  bool del = false;
  const casacore::Bool* p = arr.getStorage(del);
  std::vector<std::uint8_t> out(static_cast<std::size_t>(arr.nelements()));
  for (std::size_t i = 0; i != out.size(); ++i) out[i] = p[i] ? 1 : 0;
  arr.freeStorage(p, del);
  return out;
}

// DP3's Dysco writer does this before the storage manager sees the slot.
// If any polarization of a (baseline, channel) is flagged or non-finite, every
// polarization in that group is flagged, the visibilities become NaN, and the
// weights become 0. A flagged row flags the whole row. The casacore runs were
// written that way, so the codec has to be given the same samples.
void ApplyDyscoWriterPolicy(std::vector<std::complex<float>>* vis, std::vector<std::uint8_t>* flagged,
                            std::vector<float>* weights, const std::uint8_t* row_flag,
                            std::size_t n_baseline, std::size_t n_channel, std::size_t n_pol) {
  for (std::size_t b = 0; b != n_baseline; ++b) {
    for (std::size_t c = 0; c != n_channel; ++c) {
      const std::size_t base = (b * n_channel + c) * n_pol;
      bool bad = row_flag != nullptr && row_flag[b] != 0;
      for (std::size_t p = 0; p != n_pol && !bad; ++p) {
        const std::complex<float>& v = (*vis)[base + p];
        if ((*flagged)[base + p] != 0 || !std::isfinite(v.real()) || !std::isfinite(v.imag())) bad = true;
      }
      if (!bad) continue;
      for (std::size_t p = 0; p != n_pol; ++p) {
        (*vis)[base + p] = std::complex<float>(std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN());
        (*flagged)[base + p] = 1;
        if (weights != nullptr) (*weights)[base + p] = 0.0f;
      }
    }
  }
}

void AddFloat(float a, float b, FloatDiff* diff) {
  ++diff->n;
  const bool na = std::isnan(a);
  const bool nb = std::isnan(b);
  if (na || nb) {
    if (na && nb) {
      std::uint32_t ba = 0;
      std::uint32_t bb = 0;
      std::memcpy(&ba, &a, 4);
      std::memcpy(&bb, &b, 4);
      if (ba != bb) ++diff->nan_payload;
    } else {
      ++diff->nan_mismatch;
    }
    return;
  }
  if (!std::isfinite(a) || !std::isfinite(b)) {
    if (!(a == b)) ++diff->nan_mismatch;
    return;
  }
  diff->max_ulp = std::max(diff->max_ulp, UlpDistance(a, b));
}

void AddComplex(const std::complex<float>& a, const std::complex<float>& b, FloatDiff* diff) {
  AddFloat(a.real(), b.real(), diff);
  AddFloat(a.imag(), b.imag(), diff);
}

double RelDiff(float a, float b) {
  if (std::isnan(a) && std::isnan(b)) return 0.0;
  const double da = static_cast<double>(a);
  const double db = static_cast<double>(b);
  const double denom = std::max(std::fabs(da), std::fabs(db));
  if (denom == 0.0) return 0.0;
  return std::fabs(da - db) / denom;
}

bool BitsEqual(float a, float b) {
  std::uint32_t ba = 0;
  std::uint32_t bb = 0;
  std::memcpy(&ba, &a, 4);
  std::memcpy(&bb, &b, 4);
  if (ba == bb) return true;
  return std::isnan(a) && std::isnan(b);
}

struct CsvRow {
  std::uint32_t block;
  std::uint32_t index;
  float casacore;
  float ours;
  double rel;
  std::string kind;
};

void ConsiderSide(float casa, float ours, std::uint32_t block, std::uint32_t index, const char* kind,
                  SideDiff* diff, std::vector<CsvRow>* rows) {
  ++diff->n;
  if (BitsEqual(casa, ours)) return;
  ++diff->n_diff;
  const double rel = RelDiff(casa, ours);
  if (rel > diff->max_rel) diff->max_rel = rel;
  if (rows->size() < 5000) rows->push_back(CsvRow{block, index, casa, ours, rel, kind});
}

std::string SideStatus(const SideDiff& diff) {
  if (diff.n == 0) return "NOT_EVALUATED";
  if (diff.n_diff == 0) return "PASS";
  if (diff.max_rel <= 1e-6) return "PASS_EXPLAIN";
  return "FAIL";
}

void WriteCsv(const std::filesystem::path& path, const std::vector<CsvRow>& rows, std::uint64_t n_diff) {
  std::ofstream out(path);
  out << "kind,block,index,casacore,ours,rel_diff,explanation\n";
  out.setf(std::ios::fmtflags(0), std::ios::floatfield);
  out.precision(9);
  for (const CsvRow& row : rows) {
    out << row.kind << ',' << row.block << ',' << row.index << ',' << row.casacore << ',' << row.ours
        << ',' << row.rel << ',';
    if (row.rel <= 1e-6)
      out << '"' << kExplain << '"';
    else
      out << "\"exceeds 1e-6 relative\"";
    out << '\n';
  }
  if (n_diff > rows.size())
    out << "# truncated " << rows.size() << " of " << n_diff << " differing values\n";
}

std::vector<std::byte> WrapChunk(const ChunkHeader& header, const std::vector<float>* dictionary,
                                 const std::uint8_t* block, std::size_t block_bytes) {
  const std::size_t size = static_cast<std::size_t>(EncodedSize(header));
  std::vector<std::byte> buf(size);
  WriteHeader(header, buf.data(), buf.size());
  std::byte* pos = buf.data() + header.header_bytes;
  if (dictionary != nullptr) {
    for (float level : *dictionary) {
      std::memcpy(pos, &level, 4);
      pos += 4;
    }
  }
  if (block_bytes != static_cast<std::size_t>(BlockBytes(header)))
    throw std::runtime_error("casacore block size does not equal 4M + symbol bytes");
  std::memcpy(pos, block, block_bytes);
  return buf;
}

void HistogramBlock(const std::uint8_t* symbols, std::size_t n_symbols, unsigned bits,
                    std::vector<std::uint64_t>* hist, std::uint64_t* illegal) {
  std::vector<std::uint32_t> unpacked(n_symbols);
  UnpackSymbols(reinterpret_cast<const std::byte*>(symbols), n_symbols, bits, unpacked.data());
  const std::uint32_t limit = std::uint32_t{1} << bits;
  for (std::uint32_t symbol : unpacked) {
    if (symbol >= limit) {
      ++(*illegal);
      continue;
    }
    ++(*hist)[symbol];
  }
}

void WriteReports(const std::filesystem::path& dir, const std::string& caveat, const std::string& input,
                  unsigned bits, unsigned weight_bits, const std::vector<Check>& checks,
                  const std::vector<double>& chi2_p_vs_run0, const std::vector<ErrStats>& casa_stats,
                  const ErrStats& our_stats) {
  std::ofstream json(dir / "report.json");
  json << "{\n  \"input\": \"" << JsonEscape(input) << "\",\n  \"library\": \""
       << JsonEscape(LibraryVersion()) << "\",\n  \"bits\": " << bits
       << ",\n  \"weight_bits\": " << weight_bits << ",\n  \"caveat\": \"" << JsonEscape(caveat)
       << "\",\n  \"checks\": {\n";
  for (std::size_t i = 0; i < checks.size(); ++i) {
    const Check& c = checks[i];
    json << "    \"" << c.name << "\": {\"status\": \"" << c.status << "\", \"detail\": \""
         << JsonEscape(c.detail) << '"';
    for (const auto& num : c.numbers) json << ", \"" << num.first << "\": " << Num(num.second);
    json << '}';
    if (i + 1 != checks.size()) json << ',';
    json << '\n';
  }
  json << "  },\n  \"chi2_p_vs_run0\": [";
  for (std::size_t i = 0; i < chi2_p_vs_run0.size(); ++i) {
    if (i) json << ", ";
    json << Num(chi2_p_vs_run0[i]);
  }
  json << "],\n  \"casacore_rms_over_sigma\": [";
  for (std::size_t i = 0; i < casa_stats.size(); ++i) {
    if (i) json << ", ";
    json << Num(casa_stats[i].rms_over_sigma);
  }
  json << "],\n  \"ours\": {\"rms_over_sigma\": " << Num(our_stats.rms_over_sigma)
       << ", \"p999_over_sigma\": " << Num(our_stats.p999_over_sigma)
       << ", \"max_over_sigma\": " << Num(our_stats.max_over_sigma)
       << ", \"mean_over_sigma\": " << Num(our_stats.mean_over_sigma)
       << ", \"n_cross_components\": " << our_stats.n_cross_components
       << ", \"n_normalized\": " << our_stats.n_normalized << "}\n}\n";

  std::ofstream md(dir / "report.md");
  if (!caveat.empty()) md << caveat << "\n\n";
  md << "Library " << LibraryVersion() << ". Data bits " << bits << ". Weight bits " << weight_bits
     << ".\n\n";
  md << "| Check | Status | Detail |\n|---|---|---|\n";
  for (const Check& c : checks) md << "| " << c.name << " | " << c.status << " | " << c.detail << " |\n";
  md << "\nchi2_p_vs_run0:";
  for (double p : chi2_p_vs_run0) md << ' ' << Num(p);
  md << "\nours rms/sigma " << Num(our_stats.rms_over_sigma) << " p999/sigma "
     << Num(our_stats.p999_over_sigma) << " max/sigma " << Num(our_stats.max_over_sigma)
     << " mean/sigma " << Num(our_stats.mean_over_sigma) << "\ncasacore rms/sigma:";
  for (const ErrStats& st : casa_stats) md << ' ' << Num(st.rms_over_sigma);
  md << '\n';
}

// Largest |reconstruction - original| / sigma if each finite component is decoded
// from the farther of its two adjacent dictionary centroids. Normalization
// has no dither, so this maximum is the same for two encodes whose side data
// matches. The realized peak is a separate Bernoulli draw and is reported
// beside it.
double FartherCentroidOverSigma(const std::complex<float>* vis, const std::uint8_t* valid,
                                const std::uint8_t* cross, const float* sigma, const float* side,
                                const float* levels, std::size_t n_levels, const AntennaMap& map,
                                std::size_t n_baseline, std::size_t n_channel, std::size_t n_pol) {
  if (n_levels == 0 || side == nullptr) return 0.0;
  const float* ant = side + n_channel * n_pol;
  const float* levels_end = levels + n_levels;
  double worst = 0.0;
  for (std::size_t b = 0; b != n_baseline; ++b) {
    if (cross[b] == 0) continue;
    for (std::size_t f = 0; f != n_channel; ++f) {
      for (std::size_t p = 0; p != n_pol; ++p) {
        const std::size_t i = (b * n_channel + f) * n_pol + p;
        if (valid[i] == 0) continue;
        const float sig = sigma[b * n_pol + p];
        if (!std::isfinite(sig) || sig == 0.0f) continue;
        const double factor = static_cast<double>(side[f * n_pol + p]) *
                              static_cast<double>(ant[map.antenna1[b] + p * map.n_antenna]) *
                              static_cast<double>(ant[map.antenna2[b] + p * map.n_antenna]);
        const float parts[2] = {vis[i].real(), vis[i].imag()};
        for (float part : parts) {
          if (!std::isfinite(part)) continue;
          const double norm = factor == 0.0 ? 0.0 : static_cast<double>(part) / factor;
          const float* right = std::lower_bound(levels, levels_end, static_cast<float>(norm));
          double err = 0.0;
          if (right == levels) {
            err = std::fabs(static_cast<double>(levels[0]) * factor - static_cast<double>(part));
          } else if (right == levels_end) {
            err = std::fabs(static_cast<double>(levels[n_levels - 1]) * factor - static_cast<double>(part));
          } else {
            const double hi = static_cast<double>(*right) * factor;
            const double lo = static_cast<double>(*(right - 1)) * factor;
            err = std::max(std::fabs(hi - part), std::fabs(lo - part));
          }
          const double ratio = err / static_cast<double>(sig);
          if (std::isfinite(ratio) && ratio > worst) worst = ratio;
        }
      }
    }
  }
  return worst;
}

int Run(const Options& opt) {
  std::vector<RunCols> runs;
  runs.reserve(opt.casacore.size());
  for (const std::string& path : opt.casacore) runs.push_back(FindDysco(path));
  const auto& href = runs.front().data_file.header();
  for (const RunCols& run : runs) {
    const auto& h = run.data_file.header();
    if (h.data_bit_count != href.data_bit_count || h.weight_bit_count != href.weight_bit_count ||
        h.distribution_raw != href.distribution_raw || h.normalization != href.normalization ||
        h.truncation != href.truncation || h.rows_per_block != href.rows_per_block ||
        h.antenna_count != href.antenna_count)
      throw std::runtime_error("casacore Dysco headers disagree");
    if (!run.have_weight) throw std::runtime_error("a casacore run has no Dysco WEIGHT_SPECTRUM");
  }
  const unsigned bits = href.data_bit_count;
  const unsigned weight_bits = href.weight_bit_count;
  const Distribution distribution = MapDistribution(href.distribution_raw);
  const Normalization normalization = MapNormalization(href.normalization);
  const double truncation = distribution == Distribution::kTruncatedGaussian ? href.truncation : 0.0;
  const std::uint32_t n_baseline = href.rows_per_block;
  const std::uint32_t n_antenna = href.antenna_count;

  casacore::Table input(opt.input, casacore::TableLock(casacore::TableLock::AutoNoReadLocking),
                        casacore::Table::Old);
  casacore::ArrayColumn<casacore::Complex> data_col(input, "DATA");
  casacore::ArrayColumn<casacore::Float> weight_col(input, "WEIGHT_SPECTRUM");
  casacore::ArrayColumn<casacore::Bool> flag_col(input, "FLAG");
  casacore::ScalarColumn<casacore::Int> a1_col(input, "ANTENNA1");
  casacore::ScalarColumn<casacore::Int> a2_col(input, "ANTENNA2");
  casacore::ScalarColumn<casacore::Double> time_col(input, "TIME");
  casacore::ScalarColumn<casacore::Bool> flagrow_col(input, "FLAG_ROW");
  const casacore::IPosition shape0 = data_col.shape(0);
  if (shape0.size() != 2) throw std::runtime_error("DATA cells are not rank 2");
  const std::size_t n_pol = static_cast<std::size_t>(shape0[0]);
  const std::size_t n_channel = static_cast<std::size_t>(shape0[1]);
  const std::size_t nrow = static_cast<std::size_t>(input.nrow());
  if (n_baseline == 0 || nrow % n_baseline != 0)
    throw std::runtime_error("row count is not a multiple of Dysco rowsPerBlock");
  const std::size_t n_time = nrow / n_baseline;
  if (href.n_blocks != n_time)
    throw std::runtime_error("Dysco block count " + std::to_string(href.n_blocks) +
                            " differs from the time-slot count " + std::to_string(n_time));

  casacore::Vector<casacore::Int> a1 = a1_col.getColumn();
  casacore::Vector<casacore::Int> a2 = a2_col.getColumn();
  casacore::Vector<casacore::Double> times = time_col.getColumn();
  casacore::Vector<casacore::Bool> flagrow = flagrow_col.getColumn();
  AntennaMap map;
  map.n_antenna = n_antenna;
  map.antenna1.resize(n_baseline);
  map.antenna2.resize(n_baseline);
  std::vector<std::uint8_t> is_cross(n_baseline, 0);
  for (std::size_t b = 0; b != n_baseline; ++b) {
    map.antenna1[b] = static_cast<std::uint32_t>(a1[b]);
    map.antenna2[b] = static_cast<std::uint32_t>(a2[b]);
    if (map.antenna1[b] >= n_antenna || map.antenna2[b] >= n_antenna)
      throw std::runtime_error("antenna index outside the Dysco antenna count");
    is_cross[b] = map.antenna1[b] != map.antenna2[b] ? 1 : 0;
  }
  for (std::size_t t = 0; t != n_time; ++t) {
    const double slot_time = times[t * n_baseline];
    for (std::size_t b = 0; b != n_baseline; ++b) {
      const std::size_t row = t * n_baseline + b;
      if (times[row] != slot_time)
        throw std::runtime_error("a Dysco block is not one time slot");
      if (static_cast<std::uint32_t>(a1[row]) != map.antenna1[b] ||
          static_cast<std::uint32_t>(a2[row]) != map.antenna2[b])
        throw std::runtime_error("baseline order changes between time slots");
    }
  }

  const std::size_t per_slot = n_baseline * n_channel * n_pol;
  Shape one{1, n_baseline, n_channel, n_pol};
  const AntennaMap* map_ptr = normalization == Normalization::kAF ? &map : nullptr;
  const std::uint32_t n_ant_header = normalization == Normalization::kAF ? n_antenna : 0;
  const ChunkHeader data_header =
      MakeDataHeader(one, bits, distribution, truncation, normalization, n_ant_header, 0, 0);
  const std::size_t side_count = data_header.side_floats_per_block;
  const std::size_t data_symbols = per_slot * 2;
  const std::size_t data_block_bytes = static_cast<std::size_t>(BlockBytes(data_header));
  const auto& data_col_header = href.columns[runs.front().data.index];
  if (data_col_header.block_bytes != data_block_bytes)
    throw std::runtime_error("payload size of the first file is not 4M + symbol bytes (" +
                            std::to_string(data_col_header.block_bytes) + " vs " +
                            std::to_string(data_block_bytes) + ")");
  const ChunkHeader weight_header = MakeWeightsHeader(one, weight_bits);
  const std::size_t weight_block_bytes = static_cast<std::size_t>(BlockBytes(weight_header));
  const auto& weight_col_header = runs.front().weight_file.header().columns[runs.front().weight.index];
  if (weight_col_header.block_bytes != weight_block_bytes)
    throw std::runtime_error("weight payload size is not 4 + symbol bytes");

  std::vector<float> gsl_table;
  ChunkHeader gsl_header = data_header;
  const std::vector<float> decode_levels = dp3core::dysco::DecodeTable(bits, distribution, truncation);
  const bool have_gsl = GslAvailable();
  if (have_gsl) {
    gsl_table = GslDecodeTable(bits, distribution, truncation);
    gsl_header.flags = kFlagDictionaryEmbedded;
    gsl_header.dict_checksum = TableChecksum(gsl_table);
  }

  std::cerr << "p2 slots=" << n_time << " baselines=" << n_baseline << " channels=" << n_channel
            << " pols=" << n_pol << " bits=" << bits << " runs=" << runs.size() << '\n';

  std::vector<std::vector<float>> slot_sigma;
  slot_sigma.reserve(n_time);
  for (std::size_t t = 0; t != n_time; ++t) {
    auto vis_raw = ReadSlice(data_col, t * n_baseline, n_baseline);
    auto flagged = ReadFlags(flag_col, t * n_baseline, n_baseline);
    if (vis_raw.size() != per_slot || flagged.size() != per_slot)
      throw std::runtime_error("DATA or FLAG slice size is not baseline * channel * pol");
    std::vector<std::complex<float>> vis(per_slot);
    std::vector<std::uint8_t> valid(per_slot, 0);
    std::vector<std::uint8_t> row_flag(n_baseline, 0);
    for (std::size_t b = 0; b != n_baseline; ++b)
      row_flag[b] = flagrow[t * n_baseline + b] != casacore::False ? 1 : 0;
    std::memcpy(vis.data(), vis_raw.data(), per_slot * sizeof(std::complex<float>));
    ApplyDyscoWriterPolicy(&vis, &flagged, nullptr, row_flag.data(), n_baseline, n_channel, n_pol);
    for (std::size_t i = 0; i != per_slot; ++i) {
      const bool row_is_flagged = row_flag[i / (n_channel * n_pol)] != 0;
      const bool finite = std::isfinite(vis[i].real()) && std::isfinite(vis[i].imag());
      valid[i] = (!row_is_flagged && flagged[i] == 0 && finite) ? 1 : 0;
    }
    std::vector<float> sigma(n_baseline * n_pol);
    SlotChannelSigma(vis.data(), valid.data(), n_baseline, n_channel, n_pol, sigma.data());
    slot_sigma.push_back(std::move(sigma));
    if (t % 100 == 0) std::cerr << "sigma slot " << t << '\n';
  }
  const std::vector<float> sigma = ChannelDiffSigma(slot_sigma, n_pol);
  slot_sigma.clear();

  ErrAcc ours_acc;
  double our_ceiling = 0.0;
  std::uint64_t nonfinite_violations = 0;
  std::vector<std::uint64_t> our_hist(std::size_t{1} << bits, 0);
  std::uint64_t illegal_symbols = 0;
  std::vector<float> our_side(n_time * side_count);
  std::vector<float> our_weight_side(n_time);
  DataParams params;
  params.bits = bits;
  params.distribution = distribution;
  params.truncation = truncation;
  params.normalization = normalization;
  params.seed = 0;
  WeightParams weight_params;
  weight_params.bits = weight_bits;
  const std::size_t data_encoded_bytes = static_cast<std::size_t>(EncodedSize(data_header));
  std::vector<std::byte> encoded(data_encoded_bytes);
  std::vector<std::complex<float>> decoded(per_slot);
  for (std::size_t t = 0; t != n_time; ++t) {
    auto vis_raw = ReadSlice(data_col, t * n_baseline, n_baseline);
    auto flagged = ReadFlags(flag_col, t * n_baseline, n_baseline);
    auto weights = ReadSlice(weight_col, t * n_baseline, n_baseline);
    std::vector<std::complex<float>> vis(per_slot);
    std::memcpy(vis.data(), vis_raw.data(), per_slot * sizeof(std::complex<float>));
    std::vector<float> wvis(weights.size());
    std::memcpy(wvis.data(), weights.data(), weights.size() * sizeof(float));
    std::vector<std::uint8_t> row_flag(n_baseline, 0);
    for (std::size_t b = 0; b != n_baseline; ++b)
      row_flag[b] = flagrow[t * n_baseline + b] != casacore::False ? 1 : 0;
    ApplyDyscoWriterPolicy(&vis, &flagged, &wvis, row_flag.data(), n_baseline, n_channel, n_pol);
    std::vector<std::uint8_t> valid(per_slot, 0);
    for (std::size_t i = 0; i != per_slot; ++i) {
      const bool row_is_flagged = row_flag[i / (n_channel * n_pol)] != 0;
      const bool finite = std::isfinite(vis[i].real()) && std::isfinite(vis[i].imag());
      valid[i] = (!row_is_flagged && flagged[i] == 0 && finite) ? 1 : 0;
    }
    EncodeData(params, one, map_ptr, vis.data(), encoded.data(), encoded.size(), {});
    DecodeData(encoded.data(), encoded.size(), one, map_ptr, decoded.data());
    const std::byte* side_bytes = encoded.data() + data_header.header_bytes;
    std::memcpy(our_side.data() + t * side_count, side_bytes, side_count * sizeof(float));
    HistogramBlock(reinterpret_cast<const std::uint8_t*>(side_bytes + side_count * 4), data_symbols, bits,
                   &our_hist, &illegal_symbols);
    for (std::size_t i = 0; i != per_slot; ++i) {
      const bool in_bad = !std::isfinite(vis[i].real()) || !std::isfinite(vis[i].imag());
      const bool out_bad = !std::isfinite(decoded[i].real()) || !std::isfinite(decoded[i].imag());
      if (out_bad && !in_bad) ++nonfinite_violations;
      if (std::isinf(decoded[i].real()) || std::isinf(decoded[i].imag())) ++nonfinite_violations;
    }
    ours_acc.Add(vis.data(), decoded.data(), valid.data(), is_cross.data(), sigma.data(), n_baseline,
                 n_channel, n_pol);
    our_ceiling = std::max(
        our_ceiling, FartherCentroidOverSigma(vis.data(), valid.data(), is_cross.data(), sigma.data(),
                                              our_side.data() + t * side_count, decode_levels.data(),
                                              decode_levels.size(), map, n_baseline, n_channel, n_pol));
    const std::size_t wbytes = static_cast<std::size_t>(EncodedSize(weight_header));
    std::vector<std::byte> wenc(wbytes);
    EncodeWeights(weight_params, one, wvis.data(), wenc.data(), wenc.size(), {});
    std::memcpy(&our_weight_side[t], wenc.data() + weight_header.header_bytes, sizeof(float));
    if (t % 100 == 0) std::cerr << "encode slot " << t << '\n';
  }
  const ErrStats our_stats = ours_acc.Result();

  std::vector<std::vector<std::uint64_t>> casa_hists(runs.size(), std::vector<std::uint64_t>(our_hist.size(), 0));
  std::vector<ErrStats> casa_stats(runs.size());
  std::vector<double> casa_ceiling(runs.size(), 0.0);
  FloatDiff computed_diff;
  FloatDiff gsl_diff;
  FloatDiff weight_diff;
  SideDiff data_side;
  SideDiff weight_side;
  std::vector<CsvRow> csv_rows;
  std::uint64_t flag_diff = 0;
  std::vector<std::uint8_t> run0_flags;
  std::vector<std::uint8_t> run0_flagrow;
  std::uint64_t payload_mismatch = 0;
  std::uint64_t weight_payload_mismatch = 0;
  bool gsl_evaluated = have_gsl;

  for (std::size_t r = 0; r != runs.size(); ++r) {
    casacore::Table tab(opt.casacore[r], casacore::TableLock(casacore::TableLock::AutoNoReadLocking),
                        casacore::Table::Old);
    casacore::ArrayColumn<casacore::Complex> casa_data(tab, "DATA");
    casacore::ArrayColumn<casacore::Float> casa_weight(tab, "WEIGHT_SPECTRUM");
    casacore::ArrayColumn<casacore::Bool> casa_flag(tab, "FLAG");
    casacore::ScalarColumn<casacore::Bool> casa_flagrow(tab, "FLAG_ROW");
    casacore::Vector<casacore::Bool> casa_flagrow_all = casa_flagrow.getColumn();
    ErrAcc acc;
    std::vector<std::uint8_t> block(data_block_bytes);
    std::vector<std::uint8_t> wblock(weight_block_bytes);
    for (std::size_t t = 0; t != n_time; ++t) {
      if (runs[r].data_file.header().columns[runs[r].data.index].block_bytes != data_block_bytes)
        ++payload_mismatch;
      runs[r].data_file.ReadColumnBlock(static_cast<std::uint32_t>(t), runs[r].data.index, block.data());
      const float* casa_side = reinterpret_cast<const float*>(block.data());
      for (std::size_t k = 0; k != side_count; ++k)
        ConsiderSide(casa_side[k], our_side[t * side_count + k], static_cast<std::uint32_t>(t),
                     static_cast<std::uint32_t>(k), "data", &data_side, &csv_rows);
      HistogramBlock(block.data() + side_count * 4, data_symbols, bits, &casa_hists[r], &illegal_symbols);

      auto input_vis_raw = ReadSlice(data_col, t * n_baseline, n_baseline);
      auto input_flag = ReadFlags(flag_col, t * n_baseline, n_baseline);
      std::vector<std::complex<float>> input_vis(per_slot);
      std::memcpy(input_vis.data(), input_vis_raw.data(), per_slot * sizeof(std::complex<float>));
      std::vector<std::uint8_t> row_flag(n_baseline, 0);
      for (std::size_t b = 0; b != n_baseline; ++b)
        row_flag[b] = flagrow[t * n_baseline + b] != casacore::False ? 1 : 0;
      std::vector<std::complex<float>> policy_vis = input_vis;
      std::vector<std::uint8_t> policy_flag = input_flag;
      ApplyDyscoWriterPolicy(&policy_vis, &policy_flag, nullptr, row_flag.data(), n_baseline, n_channel,
                             n_pol);
      std::vector<std::uint8_t> valid(per_slot, 0);
      for (std::size_t i = 0; i != per_slot; ++i)
        valid[i] = (policy_flag[i] == 0 && std::isfinite(input_vis[i].real()) &&
                    std::isfinite(input_vis[i].imag()))
                       ? 1
                       : 0;
      auto casa_vis_raw = ReadSlice(casa_data, t * n_baseline, n_baseline);
      std::vector<std::complex<float>> casa_vis(per_slot);
      std::memcpy(casa_vis.data(), casa_vis_raw.data(), per_slot * sizeof(std::complex<float>));
      acc.Add(input_vis.data(), casa_vis.data(), valid.data(), is_cross.data(), sigma.data(), n_baseline,
              n_channel, n_pol);
      casa_ceiling[r] = std::max(
          casa_ceiling[r],
          FartherCentroidOverSigma(input_vis.data(), valid.data(), is_cross.data(), sigma.data(), casa_side,
                                   decode_levels.data(), decode_levels.size(), map, n_baseline, n_channel,
                                   n_pol));

      auto wrapped = WrapChunk(data_header, nullptr, block.data(), block.size());
      std::vector<std::complex<float>> decoded_block(per_slot);
      DecodeData(wrapped.data(), wrapped.size(), one, map_ptr, decoded_block.data());
      for (std::size_t i = 0; i != per_slot; ++i)
        AddComplex(decoded_block[i], casa_vis[i], &computed_diff);
      if (have_gsl) {
        auto wrapped_gsl = WrapChunk(gsl_header, &gsl_table, block.data(), block.size());
        DecodeData(wrapped_gsl.data(), wrapped_gsl.size(), one, map_ptr, decoded_block.data());
        for (std::size_t i = 0; i != per_slot; ++i)
          AddComplex(decoded_block[i], casa_vis[i], &gsl_diff);
      }

      if (runs[r].weight_file.header().columns[runs[r].weight.index].block_bytes != weight_block_bytes)
        ++weight_payload_mismatch;
      runs[r].weight_file.ReadColumnBlock(static_cast<std::uint32_t>(t), runs[r].weight.index, wblock.data());
      const float casa_wside = *reinterpret_cast<const float*>(wblock.data());
      ConsiderSide(casa_wside, our_weight_side[t], static_cast<std::uint32_t>(t), 0, "weight", &weight_side,
                   &csv_rows);
      auto wrapped_w = WrapChunk(weight_header, nullptr, wblock.data(), wblock.size());
      auto casa_w_raw = ReadSlice(casa_weight, t * n_baseline, n_baseline);
      std::vector<float> cross_w(casa_w_raw.size());
      DecodeWeights(wrapped_w.data(), wrapped_w.size(), one, cross_w.data());
      for (std::size_t i = 0; i != cross_w.size() && i != casa_w_raw.size(); ++i)
        AddFloat(cross_w[i], static_cast<float>(casa_w_raw[i]), &weight_diff);

      auto casa_flags = ReadFlags(casa_flag, t * n_baseline, n_baseline);
      if (r == 0) {
        run0_flags.insert(run0_flags.end(), casa_flags.begin(), casa_flags.end());
        for (std::size_t b = 0; b != n_baseline; ++b)
          run0_flagrow.push_back(casa_flagrow_all[t * n_baseline + b] != casacore::False ? 1 : 0);
      } else {
        const std::size_t base = t * per_slot;
        for (std::size_t i = 0; i != casa_flags.size() && base + i < run0_flags.size(); ++i)
          if ((run0_flags[base + i] != 0) != (casa_flags[i] != 0)) ++flag_diff;
        for (std::size_t b = 0; b != n_baseline; ++b) {
          const std::size_t row = t * n_baseline + b;
          const bool later = casa_flagrow_all[row] != casacore::False;
          if (row < run0_flagrow.size() && (run0_flagrow[row] != 0) != later) ++flag_diff;
        }
      }
      if (t % 100 == 0) std::cerr << "run " << r << " slot " << t << '\n';
    }
    casa_stats[r] = acc.Result();
  }

  std::vector<std::uint64_t> pooled(our_hist.size(), 0);
  for (const auto& hist : casa_hists)
    for (std::size_t i = 0; i != pooled.size(); ++i) pooled[i] += hist[i];
  double chi2 = 0.0;
  int df = 0;
  const bool chi_ok = ChiSquare(our_hist, pooled, &chi2, &df);
  const double chi_p = chi_ok ? ChiSquareP(chi2, df) : std::numeric_limits<double>::quiet_NaN();
  std::vector<double> chi2_p_vs_run0(runs.size(), std::numeric_limits<double>::quiet_NaN());
  double control_min = 1.0;
  int control_n = 0;
  for (std::size_t r = 0; r != runs.size(); ++r) {
    if (r == 0) {
      chi2_p_vs_run0[r] = 1.0;
      continue;
    }
    double c2 = 0.0;
    int cdf = 0;
    if (ChiSquare(casa_hists[r], casa_hists[0], &c2, &cdf)) {
      chi2_p_vs_run0[r] = ChiSquareP(c2, cdf);
      if (std::isfinite(chi2_p_vs_run0[r])) {
        control_min = std::min(control_min, chi2_p_vs_run0[r]);
        ++control_n;
      }
    }
  }

  auto within_rel = [](double value, double mean, double tol) {
    if (!std::isfinite(value) || !std::isfinite(mean)) return false;
    if (mean == 0.0) return value == 0.0;
    return std::fabs(value - mean) / std::fabs(mean) <= tol;
  };
  auto mean_of = [](const std::vector<ErrStats>& stats, double ErrStats::*field) {
    double sum = 0.0;
    for (const ErrStats& st : stats) sum += st.*field;
    return stats.empty() ? 0.0 : sum / static_cast<double>(stats.size());
  };
  const double rms_mean = mean_of(casa_stats, &ErrStats::rms_over_sigma);
  const double p999_mean = mean_of(casa_stats, &ErrStats::p999_over_sigma);
  double max_lo = std::numeric_limits<double>::infinity();
  double max_hi = 0.0;
  bool stats_ready = !casa_stats.empty() && our_stats.n_normalized > 0;
  for (const ErrStats& st : casa_stats) {
    if (st.n_normalized == 0) stats_ready = false;
    max_lo = std::min(max_lo, st.max_over_sigma);
    max_hi = std::max(max_hi, st.max_over_sigma);
  }

  std::vector<Check> checks;
  auto add = [&](Check c) { checks.push_back(std::move(c)); };

  Check side;
  side.name = "side_data_bit_equal";
  side.status = SideStatus(data_side);
  side.detail = side.status == "PASS_EXPLAIN" ? kExplain : "";
  side.numbers = {{"n", static_cast<double>(data_side.n)},
                  {"n_diff", static_cast<double>(data_side.n_diff)},
                  {"max_rel", data_side.max_rel}};
  add(std::move(side));

  Check cross_decode;
  cross_decode.name = "cross_decode_computed_table";
  cross_decode.status =
      (computed_diff.n > 0 && computed_diff.nan_mismatch == 0 && computed_diff.max_ulp <= 2) ? "PASS"
                                                                                            : "FAIL";
  if (computed_diff.n == 0) cross_decode.status = "NOT_EVALUATED";
  cross_decode.numbers = {{"max_ulp", static_cast<double>(computed_diff.max_ulp)},
                          {"nan_mismatch", static_cast<double>(computed_diff.nan_mismatch)},
                          {"nan_payload", static_cast<double>(computed_diff.nan_payload)}};
  add(std::move(cross_decode));

  Check gsl;
  gsl.name = "cross_decode_gsl_table";
  if (!gsl_evaluated || gsl_diff.n == 0)
    gsl.status = "NOT_EVALUATED";
  else if (gsl_diff.nan_mismatch == 0 && gsl_diff.max_ulp == 0)
    gsl.status = "PASS";
  else
    gsl.status = "FAIL";
  gsl.numbers = {{"max_ulp", static_cast<double>(gsl_diff.max_ulp)},
                 {"nan_mismatch", static_cast<double>(gsl_diff.nan_mismatch)},
                 {"nan_payload", static_cast<double>(gsl_diff.nan_payload)}};
  add(std::move(gsl));

  Check payload;
  payload.name = "payload_size";
  payload.status = payload_mismatch == 0 ? "PASS" : "FAIL";
  payload.numbers = {{"n_mismatch", static_cast<double>(payload_mismatch)},
                     {"block_bytes", static_cast<double>(data_block_bytes)}};
  add(std::move(payload));

  Check rms;
  rms.name = "rms_over_sigma";
  rms.status = stats_ready && within_rel(our_stats.rms_over_sigma, rms_mean, 0.01) ? "PASS" : "FAIL";
  if (!stats_ready) rms.status = "NOT_EVALUATED";
  rms.numbers = {{"ours", our_stats.rms_over_sigma}, {"casacore_mean", rms_mean}};
  add(std::move(rms));

  double ceiling_lo = std::numeric_limits<double>::infinity();
  double ceiling_hi = 0.0;
  for (double value : casa_ceiling) {
    ceiling_lo = std::min(ceiling_lo, value);
    ceiling_hi = std::max(ceiling_hi, value);
  }
  Check mx;
  mx.name = "max_over_sigma";
  mx.status = stats_ready && our_ceiling >= ceiling_lo && our_ceiling <= ceiling_hi ? "PASS" : "FAIL";
  if (!stats_ready) mx.status = "NOT_EVALUATED";
  mx.detail = "farther centroid of the shared dictionary; dither is not included";
  mx.numbers = {{"ours", our_ceiling},
                {"casacore_min", ceiling_lo},
                {"casacore_max", ceiling_hi},
                {"realized_ours", our_stats.max_over_sigma},
                {"realized_casacore_min", max_lo},
                {"realized_casacore_max", max_hi}};
  add(std::move(mx));

  Check p999;
  p999.name = "p999_over_sigma";
  p999.status = stats_ready && within_rel(our_stats.p999_over_sigma, p999_mean, 0.02) ? "PASS" : "FAIL";
  if (!stats_ready) p999.status = "NOT_EVALUATED";
  p999.numbers = {{"ours", our_stats.p999_over_sigma}, {"casacore_mean", p999_mean}};
  add(std::move(p999));

  Check mean;
  mean.name = "mean_error_over_sigma";
  mean.status = stats_ready && std::fabs(our_stats.mean_over_sigma) <= 2e-5 ? "PASS" : "FAIL";
  if (!stats_ready) mean.status = "NOT_EVALUATED";
  mean.numbers = {{"ours", our_stats.mean_over_sigma}};
  add(std::move(mean));

  Check chi;
  chi.name = "symbol_histogram_chi2";
  if (!chi_ok || !std::isfinite(chi_p))
    chi.status = "NOT_EVALUATED";
  else if (chi_p > 0.01)
    chi.status = "PASS";
  else if (control_n > 0 && control_min <= 0.01) {
    chi.status = "PASS_EXPLAIN";
    chi.detail = "casacore-vs-casacore control p also falls to 0.01 or below";
  } else {
    chi.status = "FAIL";
  }
  chi.numbers = {{"p", chi_p}, {"chi2", chi2}, {"df", static_cast<double>(df)}, {"control_min_p", control_min}};
  add(std::move(chi));

  Check finite;
  finite.name = "no_non_finite_decodes";
  finite.status = nonfinite_violations == 0 && illegal_symbols == 0 ? "PASS" : "FAIL";
  finite.numbers = {{"violations", static_cast<double>(nonfinite_violations)},
                    {"illegal_symbols", static_cast<double>(illegal_symbols)}};
  add(std::move(finite));

  Check flags;
  flags.name = "casacore_runs_same_flags";
  flags.status = flag_diff == 0 ? "PASS" : "FAIL";
  flags.numbers = {{"n_diff", static_cast<double>(flag_diff)}};
  add(std::move(flags));

  Check wside;
  wside.name = "weights_side_data_bit_equal";
  wside.status = SideStatus(weight_side);
  wside.detail = wside.status == "PASS_EXPLAIN" ? kExplain : "";
  wside.numbers = {{"n", static_cast<double>(weight_side.n)},
                   {"n_diff", static_cast<double>(weight_side.n_diff)},
                   {"max_rel", weight_side.max_rel}};
  add(std::move(wside));

  Check wpayload;
  wpayload.name = "weights_payload_size";
  wpayload.status = weight_payload_mismatch == 0 ? "PASS" : "FAIL";
  wpayload.numbers = {{"n_mismatch", static_cast<double>(weight_payload_mismatch)},
                      {"block_bytes", static_cast<double>(weight_block_bytes)}};
  add(std::move(wpayload));

  Check wcross;
  wcross.name = "weights_cross_decode";
  wcross.status = (weight_diff.n > 0 && weight_diff.nan_mismatch == 0 && weight_diff.max_ulp <= 2) ? "PASS"
                                                                                                   : "FAIL";
  if (weight_diff.n == 0) wcross.status = "NOT_EVALUATED";
  wcross.numbers = {{"max_ulp", static_cast<double>(weight_diff.max_ulp)},
                    {"nan_mismatch", static_cast<double>(weight_diff.nan_mismatch)}};
  add(std::move(wcross));

  const std::filesystem::path dir(opt.out);
  std::filesystem::create_directories(dir);
  if (data_side.n_diff != 0 || weight_side.n_diff != 0)
    WriteCsv(dir / "side-data-diff.csv", csv_rows, data_side.n_diff + weight_side.n_diff);
  WriteReports(dir, opt.caveat, opt.input, bits, weight_bits, checks, chi2_p_vs_run0, casa_stats, our_stats);

  int exit_code = 0;
  for (const Check& c : checks) {
    std::cerr << c.name << ' ' << c.status << '\n';
    if (c.status == "FAIL" || c.status == "NOT_EVALUATED") exit_code = 1;
  }
  return exit_code;
}

}  // namespace

int main(int argc, char** argv) {
  std::string out_dir;
  try {
    const Options opt = Parse(argc, argv);
    out_dir = opt.out;
    return Run(opt);
  } catch (const std::exception& e) {
    std::cerr << "dp3core_dysco_p2: " << e.what() << '\n';
    if (!out_dir.empty()) {
      try {
        std::filesystem::create_directories(out_dir);
        std::ofstream json(std::filesystem::path(out_dir) / "report.json");
        json << "{\"error\": \"" << JsonEscape(e.what()) << "\"}\n";
        std::ofstream md(std::filesystem::path(out_dir) / "report.md");
        md << "I/O or usage error: " << e.what() << '\n';
      } catch (...) {
      }
    }
    return std::string(e.what()) == "usage" ? 2 : 2;
  }
}
