// Round trip of "dp3core.dysco" / "dp3core.dysco-weights" through
// TensorStore's zarr3 driver. The codecs are found only through the static
// registration in dp3core_dysco::tensorstore (this file never references it).
// SPDX-License-Identifier: GPL-3.0-only
#include <unistd.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "check.h"
#include "dp3core/dysco/dither.h"
#include "fixtures.h"
#include "tensorstore/array.h"
#include "tensorstore/context.h"
#include "tensorstore/data_type.h"
#include "tensorstore/open.h"
#include "tensorstore/open_mode.h"
#include "tensorstore/tensorstore.h"

using namespace dp3core::dysco;
namespace fs = std::filesystem;

namespace {

fs::path TempDir(const std::string& name) {
  const char* base = std::getenv("TEST_TMPDIR");
  fs::path p = fs::path(base ? base : fs::temp_directory_path().string()) /
               ("dp3core_dysco_ts_" + name + "_" + std::to_string(::getpid()));
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

std::string ReadFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

nlohmann::json DataCodec(const testing::Fixture& f, const std::string& norm) {
  nlohmann::json c = {{"normalization", norm}, {"seed", testing::kGoldenSeed}};
  if (norm == "af") {
    c["n_antennas"] = f.map.n_antenna;
    c["antenna1"] = f.map.antenna1;
    c["antenna2"] = f.map.antenna2;
  }
  return {{"name", "dp3core.dysco"}, {"configuration", c}};
}

nlohmann::json Spec(const fs::path& dir, const std::vector<std::size_t>& shape,
                    const std::vector<std::size_t>& chunks, const std::string& dtype,
                    nlohmann::json codecs) {
  return {{"driver", "zarr3"},
          {"kvstore", {{"driver", "file"}, {"path", dir.string() + "/"}}},
          {"metadata",
           {{"shape", shape},
            {"chunk_grid", {{"name", "regular"}, {"configuration", {{"chunk_shape", chunks}}}}},
            {"data_type", dtype},
            {"fill_value", dtype == "complex64" ? nlohmann::json::array({0.0, 0.0}) : nlohmann::json(0.0)},
            {"codecs", codecs}}}};
}

template <typename T>
tensorstore::SharedArray<T, 4> ToArray(const std::vector<T>& v, const Shape& s) {
  auto a = tensorstore::AllocateArray<T>({static_cast<tensorstore::Index>(s.n_time),
                                          static_cast<tensorstore::Index>(s.n_baseline),
                                          static_cast<tensorstore::Index>(s.n_channel),
                                          static_cast<tensorstore::Index>(s.n_pol)});
  std::copy(v.begin(), v.end(), a.data());
  return a;
}

std::vector<std::size_t> Vec(const Shape& s) {
  return {s.n_time, s.n_baseline, s.n_channel, s.n_pol};
}

}  // namespace

TEST_CASE(GoldenDataThroughTensorStore) {
  const auto f = testing::MakeFixture(3, 5, 4, 4, 12);
  const char* norms[] = {"row", "rf", "af"};
  for (int k = 0; k != 3; ++k) {
    const fs::path dir = TempDir(norms[k]);
    auto spec = Spec(dir, Vec(f.shape), Vec(f.shape), "complex64",
                     nlohmann::json::array({DataCodec(f, norms[k])}));
    auto store = tensorstore::Open<std::complex<float>, 4>(
                     spec, tensorstore::OpenMode::create, tensorstore::ReadWriteMode::read_write)
                     .result();
    if (!store.ok()) std::cerr << store.status() << '\n';
    CHECK(store.ok());
    if (!store.ok()) continue;
    const auto st = tensorstore::Write(ToArray(f.data, f.shape), *store).commit_future.result();
    if (!st.ok()) std::cerr << st.status() << '\n';
    CHECK(st.ok());
    const std::string chunk = ReadFile(dir / "c" / "0" / "0" / "0" / "0");
    CHECK_EQ(Xxh3(chunk.data(), chunk.size()), testing::kGolden[k][0]);
    // Re-open from the stored zarr.json (resolved spec written by TensorStore).
    auto reopened = tensorstore::Open<std::complex<float>, 4>(
                        {{"driver", "zarr3"},
                         {"kvstore", {{"driver", "file"}, {"path", dir.string() + "/"}}}},
                        tensorstore::OpenMode::open, tensorstore::ReadWriteMode::read)
                        .result();
    CHECK(reopened.ok());
    if (!reopened.ok()) continue;
    auto back = tensorstore::Read(*reopened).result();
    CHECK(back.ok());
    if (!back.ok()) continue;
    CHECK_EQ(Xxh3(back->data(), f.data.size() * 8), testing::kGolden[k][1]);
    const auto meta = nlohmann::json::parse(ReadFile(dir / "zarr.json"));
    const auto& cfg = meta["codecs"][0]["configuration"];
    CHECK_EQ(meta["codecs"][0]["name"].get<std::string>(), std::string("dp3core.dysco"));
    CHECK_EQ(cfg["seed"].get<std::uint64_t>(), testing::kGoldenSeed);
    CHECK_EQ(cfg["bits"].get<int>(), 10);
    CHECK_EQ(cfg["prng"].get<std::string>(), std::string("splitmix64-v1"));
    fs::remove_all(dir);
  }
}

TEST_CASE(GoldenWeightsThroughTensorStore) {
  const auto f = testing::MakeFixture(3, 5, 4, 4, 12);
  const fs::path dir = TempDir("weights");
  auto spec = Spec(dir, Vec(f.shape), Vec(f.shape), "float32",
                   nlohmann::json::array({{{"name", "dp3core.dysco-weights"}}}));
  auto store = tensorstore::Open<float, 4>(spec, tensorstore::OpenMode::create,
                                           tensorstore::ReadWriteMode::read_write)
                   .result();
  if (!store.ok()) std::cerr << store.status() << '\n';
  CHECK(store.ok());
  if (!store.ok()) return;
  CHECK(tensorstore::Write(ToArray(f.weights, f.shape), *store).commit_future.result().ok());
  const std::string chunk = ReadFile(dir / "c" / "0" / "0" / "0" / "0");
  CHECK_EQ(Xxh3(chunk.data(), chunk.size()), testing::kGolden[3][0]);
  auto back = tensorstore::Read(*store).result();
  CHECK(back.ok());
  if (back.ok()) CHECK_EQ(Xxh3(back->data(), f.weights.size() * 4), testing::kGolden[3][1]);
  fs::remove_all(dir);
}

TEST_CASE(MultiChunkWithZstdAndSharding) {
  const auto f = testing::MakeFixture(5, 4, 6, 4, 99);
  // (a) plain chunks + crc32c + zstd, edge chunks padded along time.
  const fs::path dir = TempDir("multi");
  auto codecs = nlohmann::json::array({DataCodec(f, "af"), {{"name", "crc32c"}},
                                       {{"name", "zstd"}, {"configuration", {{"level", 3}}}}});
  auto store = tensorstore::Open<std::complex<float>, 4>(
                   Spec(dir, Vec(f.shape), {2, f.shape.n_baseline, 3, 4}, "complex64", codecs),
                   tensorstore::OpenMode::create, tensorstore::ReadWriteMode::read_write)
                   .result();
  if (!store.ok()) std::cerr << store.status() << '\n';
  CHECK(store.ok());
  if (!store.ok()) return;
  CHECK(tensorstore::Write(ToArray(f.data, f.shape), *store).commit_future.result().ok());
  auto back = tensorstore::Read(*store).result();
  CHECK(back.ok());
  // Expected: each (padded) chunk encoded/decoded directly with the core.
  DataParams p;
  p.seed = testing::kGoldenSeed;
  std::size_t mismatches = 0;
  const std::size_t B = f.shape.n_baseline;
  for (std::size_t t0 = 0; t0 < 5; t0 += 2)
    for (std::size_t c0 = 0; c0 < 6; c0 += 3) {
      const Shape cs{2, B, 3, 4};
      std::vector<std::complex<float>> blk(2 * B * 3 * 4, 0.0f);
      for (std::size_t t = 0; t != 2 && t0 + t < 5; ++t)
        for (std::size_t b = 0; b != B; ++b)
          for (std::size_t c = 0; c != 3; ++c)
            for (std::size_t q = 0; q != 4; ++q)
              blk[((t * B + b) * 3 + c) * 4 + q] = f.data[(((t0 + t) * B + b) * 6 + c0 + c) * 4 + q];
      std::vector<std::byte> enc(EncodedSize(p, cs, &f.map));
      EncodeData(p, cs, &f.map, blk.data(), enc.data(), enc.size());
      std::vector<std::complex<float>> dec(blk.size());
      DecodeData(enc.data(), enc.size(), cs, &f.map, dec.data());
      for (std::size_t t = 0; t != 2 && t0 + t < 5; ++t)
        for (std::size_t b = 0; b != B; ++b)
          for (std::size_t c = 0; c != 3; ++c)
            for (std::size_t q = 0; q != 4; ++q) {
              const auto got = (*back)(static_cast<tensorstore::Index>(t0 + t),
                                       static_cast<tensorstore::Index>(b),
                                       static_cast<tensorstore::Index>(c0 + c),
                                       static_cast<tensorstore::Index>(q));
              mismatches += got != dec[((t * B + b) * 3 + c) * 4 + q];
            }
    }
  CHECK_EQ(mismatches, std::size_t{0});
  fs::remove_all(dir);

  // (b) sharding_indexed with the codec inside: same values as (a)'s layout
  // would give for inner chunks (1, B, 3, 4); check the round-trip error.
  const fs::path sdir = TempDir("shard");
  nlohmann::json inner = DataCodec(f, "rf");
  auto shard = nlohmann::json::array(
      {{{"name", "sharding_indexed"},
        {"configuration",
         {{"chunk_shape", {1, B, 3, 4}},
          {"codecs", nlohmann::json::array({inner})},
          {"index_codecs", nlohmann::json::array({{{"name", "bytes"}, {"configuration", {{"endian", "little"}}}},
                                                  {{"name", "crc32c"}}})}}}}});
  auto sstore = tensorstore::Open<std::complex<float>, 4>(
                    Spec(sdir, Vec(f.shape), {5, B, 6, 4}, "complex64", shard),
                    tensorstore::OpenMode::create, tensorstore::ReadWriteMode::read_write)
                    .result();
  if (!sstore.ok()) std::cerr << sstore.status() << '\n';
  CHECK(sstore.ok());
  if (!sstore.ok()) return;
  CHECK(tensorstore::Write(ToArray(f.data, f.shape), *sstore).commit_future.result().ok());
  auto sback = tensorstore::Read(*sstore).result();
  CHECK(sback.ok());
  double err = 0, ref = 0;
  for (std::size_t i = 0; i != f.data.size(); ++i) {
    err += std::norm(std::complex<double>(sback->data()[i]) - std::complex<double>(f.data[i]));
    ref += std::norm(std::complex<double>(f.data[i]));
  }
  CHECK(std::sqrt(err / ref) < 0.01);
  fs::remove_all(sdir);
}

TEST_CASE(InvalidConfigurationsAreRejected) {
  const auto f = testing::MakeFixture(1, 3, 2, 2, 1);
  const fs::path dir = TempDir("invalid");
  auto open = [&](nlohmann::json codec, const std::string& dtype = "complex64") {
    return tensorstore::Open(Spec(dir, Vec(f.shape), Vec(f.shape), dtype,
                                  nlohmann::json::array({codec})),
                             tensorstore::OpenMode::create | tensorstore::OpenMode::delete_existing)
        .result();
  };
  auto s1 = open({{"name", "dp3core.dysco"}, {"configuration", {{"normalization", "af"}}}});
  CHECK(!s1.ok());
  if (!s1.ok()) CHECK(std::string(s1.status().message()).find("requires n_antennas") != std::string::npos);
  auto s2 = open({{"name", "dp3core.dysco"}, {"configuration", {{"normalization", "row"}, {"bits", 5}}}});
  CHECK(!s2.ok());
  auto s3 = open({{"name", "dp3core.dysco"}, {"configuration", {{"normalization", "row"}}}}, "float32");
  CHECK(!s3.ok());
  if (!s3.ok()) CHECK(std::string(s3.status().message()).find("complex64") != std::string::npos);
  auto s4 = open({{"name", "dp3core.dysco"},
                  {"configuration", {{"normalization", "row"},
                                     {"axes", {"baseline", "time", "frequency", "polarization"}}}}});
  CHECK(!s4.ok());
  auto s5 = open({{"name", "dp3core.dysco"}, {"configuration", {{"normalization", "row"}, {"seed", "123"}}}});
  CHECK(s5.ok());
  fs::remove_all(dir);
}
