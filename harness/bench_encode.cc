// Encode and decode one visibility dump, one time slot per chunk.
// SPDX-License-Identifier: GPL-3.0-only
//
// Usage:
//   dp3core_dysco_bench --dump DIR --kind data|weights --bits N
//       --out-decoded FILE --json FILE [--apply-writer-policy]
// The dump is an msextract directory (extract_info.txt, data.c64 or ws.f32,
// a1.i32, a2.i32, time.f64). With --apply-writer-policy the dump also has
// flag.u8 and flagrow.u8. That flag is DP3's Dysco writer rule: a flagged or
// non-finite correlation flags the whole baseline-channel group, the
// visibilities become NaN, and the weights become 0. The same rule is in
// harness/p2_harness.cc (ApplyDyscoWriterPolicy).
#include <algorithm>
#include <chrono>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dp3core/dysco/codec.h"

namespace {

std::string Need(int& i, int argc, char** argv) {
  if (i + 1 >= argc) throw std::runtime_error("missing value");
  return argv[++i];
}

std::string InfoValue(const std::string& path, const std::string& key) {
  std::ifstream in(path);
  std::string k;
  std::string v;
  while (in >> k >> v)
    if (k == key) return v;
  throw std::runtime_error("missing " + key + " in " + path);
}

template <typename T>
std::vector<T> ReadAll(const std::string& path, std::size_t n) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::vector<T> out(n);
  in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n * sizeof(T)));
  if (!in) throw std::runtime_error("short read " + path);
  return out;
}

// Same transformation as ApplyDyscoWriterPolicy in p2_harness.cc.
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

struct PolicyFiles {
  std::ifstream flag;
  std::ifstream flagrow;
  std::ifstream data;
};

void ReadPolicy(PolicyFiles* files, std::size_t per, std::size_t nbl, std::vector<std::uint8_t>* flagged,
                std::vector<std::uint8_t>* row_flag) {
  flagged->resize(per);
  row_flag->resize(nbl);
  files->flag.read(reinterpret_cast<char*>(flagged->data()), static_cast<std::streamsize>(per));
  files->flagrow.read(reinterpret_cast<char*>(row_flag->data()), static_cast<std::streamsize>(nbl));
  if (!files->flag || !files->flagrow) throw std::runtime_error("short flag read");
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string dump;
    std::string kind = "data";
    unsigned bits = 10;
    std::string out_path;
    std::string json_path;
    bool policy = false;
    for (int i = 1; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--dump")
        dump = Need(i, argc, argv);
      else if (a == "--kind")
        kind = Need(i, argc, argv);
      else if (a == "--bits")
        bits = static_cast<unsigned>(std::stoul(Need(i, argc, argv)));
      else if (a == "--out-decoded")
        out_path = Need(i, argc, argv);
      else if (a == "--json")
        json_path = Need(i, argc, argv);
      else if (a == "--apply-writer-policy")
        policy = true;
      else
        throw std::runtime_error("unknown argument " + a);
    }
    if (dump.empty() || out_path.empty() || json_path.empty())
      throw std::runtime_error(
          "usage: dp3core_dysco_bench --dump DIR --kind data|weights --bits N "
          "--out-decoded FILE --json FILE [--apply-writer-policy]");
    const auto nrow = static_cast<std::size_t>(std::stoull(InfoValue(dump + "/extract_info.txt", "nrow")));
    const auto nchan = static_cast<std::size_t>(std::stoull(InfoValue(dump + "/extract_info.txt", "nchan")));
    const auto npol = static_cast<std::size_t>(std::stoull(InfoValue(dump + "/extract_info.txt", "npol")));
    const auto time = ReadAll<double>(dump + "/time.f64", nrow);
    std::size_t nbl = 1;
    while (nbl < nrow && time[nbl] == time[0]) ++nbl;
    if (nrow % nbl != 0) throw std::runtime_error("row count is not a multiple of the first time slot");
    const std::size_t ntime = nrow / nbl;
    const auto a1 = ReadAll<std::int32_t>(dump + "/a1.i32", nrow);
    const auto a2 = ReadAll<std::int32_t>(dump + "/a2.i32", nrow);
    dp3core::dysco::AntennaMap map;
    map.antenna1.resize(nbl);
    map.antenna2.resize(nbl);
    std::uint32_t n_antenna = 0;
    for (std::size_t b = 0; b != nbl; ++b) {
      map.antenna1[b] = static_cast<std::uint32_t>(a1[b]);
      map.antenna2[b] = static_cast<std::uint32_t>(a2[b]);
      n_antenna = std::max(n_antenna, std::max(map.antenna1[b], map.antenna2[b]) + 1);
    }
    map.n_antenna = n_antenna;
    for (std::size_t t = 1; t != ntime; ++t) {
      for (std::size_t b = 0; b != nbl; ++b) {
        if (a1[t * nbl + b] != a1[b] || a2[t * nbl + b] != a2[b])
          throw std::runtime_error("baseline order changes between time slots");
      }
    }
    dp3core::dysco::Shape shape{1, nbl, nchan, npol};
    const std::size_t per = nbl * nchan * npol;
    std::uint64_t encoded_bytes = 0;
    double encode_s = 0.0;
    double decode_s = 0.0;
    std::ofstream out(out_path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + out_path);
    PolicyFiles policy_files;
    if (policy) {
      policy_files.flag.open(dump + "/flag.u8", std::ios::binary);
      policy_files.flagrow.open(dump + "/flagrow.u8", std::ios::binary);
      if (!policy_files.flag || !policy_files.flagrow)
        throw std::runtime_error("writer policy needs flag.u8 and flagrow.u8 in the dump");
      if (kind == "weights") {
        policy_files.data.open(dump + "/data.c64", std::ios::binary);
        if (!policy_files.data) throw std::runtime_error("writer policy on weights needs data.c64");
      }
    }
    if (kind == "data") {
      std::ifstream in(dump + "/data.c64", std::ios::binary);
      if (!in) throw std::runtime_error("cannot open data.c64");
      dp3core::dysco::DataParams params;
      params.bits = bits;
      params.distribution = dp3core::dysco::Distribution::kTruncatedGaussian;
      params.truncation = 2.5;
      params.normalization = dp3core::dysco::Normalization::kAF;
      params.seed = 0;
      const std::size_t nbytes = dp3core::dysco::EncodedSize(params, shape, &map);
      std::vector<std::byte> buf(nbytes);
      std::vector<std::complex<float>> slot(per);
      std::vector<std::complex<float>> decoded(per);
      std::vector<std::uint8_t> flagged;
      std::vector<std::uint8_t> row_flag;
      for (std::size_t t = 0; t != ntime; ++t) {
        in.read(reinterpret_cast<char*>(slot.data()), static_cast<std::streamsize>(per * sizeof(slot[0])));
        if (!in) throw std::runtime_error("short data.c64");
        if (policy) {
          ReadPolicy(&policy_files, per, nbl, &flagged, &row_flag);
          ApplyDyscoWriterPolicy(&slot, &flagged, nullptr, row_flag.data(), nbl, nchan, npol);
        }
        const auto t0 = std::chrono::steady_clock::now();
        dp3core::dysco::EncodeData(params, shape, &map, slot.data(), buf.data(), buf.size(), {});
        encode_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        encoded_bytes += nbytes;
        const auto t1 = std::chrono::steady_clock::now();
        dp3core::dysco::DecodeData(buf.data(), buf.size(), shape, &map, decoded.data());
        decode_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        out.write(reinterpret_cast<const char*>(decoded.data()),
                  static_cast<std::streamsize>(per * sizeof(decoded[0])));
      }
    } else if (kind == "weights") {
      std::ifstream in(dump + "/ws.f32", std::ios::binary);
      if (!in) throw std::runtime_error("cannot open ws.f32");
      dp3core::dysco::WeightParams params;
      params.bits = bits;
      const std::size_t nbytes = dp3core::dysco::EncodedSize(params, shape);
      std::vector<std::byte> buf(nbytes);
      std::vector<float> slot(per);
      std::vector<float> decoded(per);
      std::vector<std::complex<float>> vis;
      std::vector<std::uint8_t> flagged;
      std::vector<std::uint8_t> row_flag;
      if (policy) vis.resize(per);
      for (std::size_t t = 0; t != ntime; ++t) {
        in.read(reinterpret_cast<char*>(slot.data()), static_cast<std::streamsize>(per * sizeof(float)));
        if (!in) throw std::runtime_error("short ws.f32");
        if (policy) {
          policy_files.data.read(reinterpret_cast<char*>(vis.data()),
                                 static_cast<std::streamsize>(per * sizeof(vis[0])));
          if (!policy_files.data) throw std::runtime_error("short data.c64");
          ReadPolicy(&policy_files, per, nbl, &flagged, &row_flag);
          ApplyDyscoWriterPolicy(&vis, &flagged, &slot, row_flag.data(), nbl, nchan, npol);
        }
        const auto t0 = std::chrono::steady_clock::now();
        dp3core::dysco::EncodeWeights(params, shape, slot.data(), buf.data(), buf.size(), {});
        encode_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        encoded_bytes += nbytes;
        const auto t1 = std::chrono::steady_clock::now();
        dp3core::dysco::DecodeWeights(buf.data(), buf.size(), shape, decoded.data());
        decode_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        out.write(reinterpret_cast<const char*>(decoded.data()),
                  static_cast<std::streamsize>(per * sizeof(float)));
      }
    } else {
      throw std::runtime_error("kind must be data or weights");
    }
    std::ofstream js(json_path);
    js << "{\"encode_s\": " << encode_s << ", \"decode_s\": " << decode_s
       << ", \"encoded_bytes\": " << encoded_bytes << ", \"n_time\": " << ntime
       << ", \"n_baseline\": " << nbl << ", \"n_channel\": " << nchan << ", \"n_pol\": " << npol
       << ", \"bits\": " << bits << ", \"kind\": \"" << kind << "\""
       << ", \"writer_policy\": " << (policy ? "true" : "false") << "}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "dp3core_dysco_bench: " << e.what() << '\n';
    return 2;
  }
}
