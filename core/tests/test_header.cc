// Tests for the chunk header (spec/SPEC.md section 5).
// SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "check.h"
#include "dp3core/dysco/header.h"
#include "dp3core/dysco/tables.h"

using namespace dp3core::dysco;

namespace {
const Shape kShape{3, 703, 64, 4};

std::vector<std::byte> Serialize(const ChunkHeader& h) {
  std::vector<std::byte> bytes(kHeaderBytes);
  WriteHeader(h, bytes.data(), bytes.size());
  return bytes;
}

// Header plus a zero payload of the right size.
std::vector<std::byte> FullChunk(const ChunkHeader& h) {
  std::vector<std::byte> bytes = Serialize(h);
  bytes.resize(EncodedSize(h));
  return bytes;
}

std::string Hex(const std::vector<std::byte>& bytes, std::size_t n) {
  std::string s;
  char buf[3];
  for (std::size_t i = 0; i != n; ++i) {
    std::snprintf(buf, sizeof(buf), "%02x", std::to_integer<unsigned>(bytes[i]));
    s += buf;
  }
  return s;
}

void SetU32(std::vector<std::byte>& b, std::size_t off, std::uint32_t v) {
  for (int i = 0; i != 4; ++i)
    b[off + static_cast<std::size_t>(i)] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
}

std::uint64_t Mix(std::uint64_t z) {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
}  // namespace

TEST_CASE(DataHeaderDerivedSizes) {
  const ChunkHeader af = MakeDataHeader(kShape, 10, Distribution::kTruncatedGaussian,
                                        2.5, Normalization::kAF, 38);
  CHECK_EQ(af.side_floats_per_block, 4u * (64u + 38u));
  CHECK_EQ(af.symbol_bytes_per_block, (2u * 703u * 64u * 4u * 10u + 7u) / 8u);
  CHECK_EQ(BlockBytes(af), 4u * 408u + 449920u);
  CHECK_EQ(EncodedSize(af), 80u + 3u * (4u * 408u + 449920u));
  CHECK_EQ(af.dict_checksum,
           TableChecksum(DecodeTable(10, Distribution::kTruncatedGaussian, 2.5)));

  const ChunkHeader rf = MakeDataHeader(kShape, 6, Distribution::kGaussian, 0.0,
                                        Normalization::kRF, 0);
  CHECK_EQ(rf.side_floats_per_block, 4u * (64u + 703u));
  CHECK_EQ(rf.n_antenna, 0u);
  const ChunkHeader row = MakeDataHeader(kShape, 3, Distribution::kUniform, 0.0,
                                         Normalization::kRow, 99);
  CHECK_EQ(row.side_floats_per_block, 703u);
  CHECK_EQ(row.n_antenna, 0u);  // ignored unless AF
  // 2*703*64*4*3 bits = 1079808 bits = 134976 bytes exactly.
  CHECK_EQ(row.symbol_bytes_per_block, 134976u);
  // Truncation is stored only for the truncated Gaussian.
  const ChunkHeader g = MakeDataHeader(kShape, 8, Distribution::kGaussian, 2.5,
                                       Normalization::kRow, 0);
  CHECK(g.truncation == 0.0 && !std::signbit(g.truncation));
}

TEST_CASE(OptionalSectionsAndOddSizes) {
  const Shape odd{1, 3, 1, 1};  // 6 symbols
  const ChunkHeader h = MakeDataHeader(odd, 3, Distribution::kGaussian, 0.0,
                                       Normalization::kAF, 3,
                                       kFlagDictionaryEmbedded | kFlagAntennaMapEmbedded);
  CHECK_EQ(h.symbol_bytes_per_block, 3u);  // 18 bits -> 3 bytes
  CHECK_EQ(DictionaryBytes(h), 4u * 7u);
  CHECK_EQ(AntennaMapBytes(h), 8u * 3u);
  CHECK_EQ(EncodedSize(h), 80u + 28u + 24u + 4u * 4u + 3u);
}

TEST_CASE(WeightsHeader) {
  const ChunkHeader w = MakeWeightsHeader(kShape, 12);
  CHECK(w.kind == Kind::kWeights);
  CHECK_EQ(unsigned(w.distribution), 255u);
  CHECK_EQ(unsigned(w.normalization), 255u);
  CHECK_EQ(w.side_floats_per_block, 1u);
  CHECK_EQ(w.symbol_bytes_per_block, 703u * 64u * 12u / 8u);
  CHECK_EQ(w.dict_checksum, 0u);
  CHECK_THROWS(MakeWeightsHeader(kShape, 7), std::invalid_argument, "unsupported bit count 7");
}

TEST_CASE(RoundTripAllCombinations) {
  const unsigned bits[] = {2, 3, 4, 6, 8, 10, 12};  // 16 is slow and covered elsewhere
  const Distribution ds[] = {Distribution::kGaussian, Distribution::kTruncatedGaussian,
                             Distribution::kUniform};
  const Normalization ns[] = {Normalization::kAF, Normalization::kRF, Normalization::kRow};
  for (unsigned b : bits)
    for (Distribution d : ds)
      for (Normalization n : ns) {
        const ChunkHeader h = MakeDataHeader({2, 10, 5, 2}, b, d, 3.0, n, 5, 0,
                                             0x0123456789ABCDEFull);
        const std::vector<std::byte> bytes = FullChunk(h);
        const ChunkHeader r = ReadHeader(bytes.data(), bytes.size());
        CHECK(Serialize(r) == Serialize(h));
      }
  const ChunkHeader w = MakeWeightsHeader({2, 10, 5, 2}, 16);
  const std::vector<std::byte> bytes = FullChunk(w);
  CHECK(Serialize(ReadHeader(bytes.data(), bytes.size())) == Serialize(w));
}

// Byte-exact layout (spec table 5.1), little-endian.
TEST_CASE(GoldenBytes) {
  const ChunkHeader h = MakeDataHeader({2, 3, 4, 1}, 4, Distribution::kTruncatedGaussian,
                                       2.5, Normalization::kAF, 3, kFlagAntennaMapEmbedded,
                                       0x1122334455667788ull);
  const std::vector<std::byte> b = Serialize(h);
  const std::string expected_prefix =
      "445033445953434f"  // "DP3DYSCO"
      "0100" "5000"       // version 1, header_bytes 80
      "01" "04" "01" "00" // data, 4 bits, truncated_gaussian, af
      "02000000"          // flags: antenna map
      "02000000" "03000000" "04000000" "01000000"  // T B F P
      "03000000"          // n_antenna
      "0000000000000440"  // 2.5 as little-endian double
      "8877665544332211"; // nonce
  CHECK_EQ(Hex(b, 56), expected_prefix);
  const std::string expected_tail =
      "07000000"          // M = 1 * (4 + 3)
      "0c000000"          // Y = ceil(2*3*4*1*4 / 8) = 12
      "0000000000000000"; // reserved
  std::vector<std::byte> tail(b.begin() + 64, b.end());
  CHECK_EQ(Hex(tail, 16), expected_tail);
}

TEST_CASE(ReaderRejectsInvalidChunks) {
  const ChunkHeader h = MakeDataHeader(kShape, 10, Distribution::kTruncatedGaussian,
                                       2.5, Normalization::kAF, 38);
  const std::vector<std::byte> good = FullChunk(h);
  auto mutated = [&](std::size_t offset, std::uint8_t value) {
    std::vector<std::byte> b = good;
    b[offset] = static_cast<std::byte>(value);
    return b;
  };
  auto read = [](const std::vector<std::byte>& b) { return ReadHeader(b.data(), b.size()); };

  CHECK_THROWS(ReadHeader(good.data(), 5), FormatError, "too short");
  CHECK_THROWS(ReadHeader(good.data(), 40, false), FormatError, "shorter than the 80-byte header");
  CHECK_THROWS(ReadHeader(good.data(), good.size() - 1), FormatError, "header implies");
  CHECK_THROWS(read(mutated(0, 'X')), FormatError, "bad magic");
  CHECK_THROWS(read(mutated(8, 2)), FormatError, "unsupported format_version 2");
  CHECK_THROWS(read(mutated(8, 0)), FormatError, "unsupported format_version 0");
  CHECK_THROWS(read(mutated(10, 79)), FormatError, "header_bytes 79 < 80");
  CHECK_THROWS(read(mutated(12, 3)), FormatError, "invalid kind code 3");
  CHECK_THROWS(read(mutated(13, 11)), FormatError, "unsupported bits 11");
  CHECK_THROWS(read(mutated(14, 3)), FormatError, "invalid distribution code 3");
  CHECK_THROWS(read(mutated(15, 255)), FormatError, "invalid normalization code 255");
  CHECK_THROWS(read(mutated(16, 4)), FormatError, "unknown flag bits");
  CHECK_THROWS(read(mutated(20, 0)), FormatError, "zero-sized dimension");
  CHECK_THROWS(read(mutated(36, 0)), FormatError, "AF needs n_antenna");
  CHECK_THROWS(read(mutated(47, 0x7F)), FormatError, "truncation must be in (0, 8]");
  CHECK_THROWS(read(mutated(72, 1)), FormatError, "reserved field");
  CHECK_THROWS(read(mutated(64, 0)), FormatError, "side_floats_per_block");
  CHECK_THROWS(read(mutated(68, 0)), FormatError, "symbol_bytes_per_block");
  // A consistent header that does not fit the buffer length.
  {
    std::vector<std::byte> b = good;
    SetU32(b, 20, 4);  // T = 4, length still for T = 3
    CHECK_THROWS(read(b), FormatError, "header implies");
  }
  // Weights chunk carrying data codes / a checksum.
  {
    std::vector<std::byte> w = FullChunk(MakeWeightsHeader(kShape, 12));
    w[14] = std::byte{0};
    CHECK_THROWS(read(w), FormatError, "distribution and normalization 255");
    w = FullChunk(MakeWeightsHeader(kShape, 12));
    w[16] = std::byte{1};
    CHECK_THROWS(read(w), FormatError, "embedded dictionary is only allowed for data");
    w = FullChunk(MakeWeightsHeader(kShape, 12));
    w[56] = std::byte{1};
    CHECK_THROWS(read(w), FormatError, "dict_checksum 0");
  }
  // Antenna map on RF data; truncation on a Gaussian table.
  {
    ChunkHeader rf = MakeDataHeader(kShape, 10, Distribution::kGaussian, 0.0,
                                    Normalization::kRF, 0);
    std::vector<std::byte> b = FullChunk(rf);
    b[16] = std::byte{2};
    CHECK_THROWS(read(b), FormatError, "embedded antenna map is only allowed for AF");
    b = FullChunk(rf);
    b[47] = std::byte{0x40};
    CHECK_THROWS(read(b), FormatError, "truncation must be +0.0");
    b = FullChunk(rf);
    b[47] = std::byte{0x80};  // -0.0
    CHECK_THROWS(read(b), FormatError, "truncation must be +0.0");
    b = FullChunk(rf);
    b[36] = std::byte{5};
    CHECK_THROWS(read(b), FormatError, "n_antenna must be 0");
  }
}

TEST_CASE(ForwardCompatibleHeaderLength) {
  // A longer header (a later minor revision) is skipped by readers.
  ChunkHeader h = MakeDataHeader({1, 2, 1, 1}, 8, Distribution::kUniform, 0.0,
                                 Normalization::kRow, 0);
  std::vector<std::byte> b = Serialize(h);
  b[10] = std::byte{96};                       // header_bytes = 96
  b.insert(b.begin() + 80, 16, std::byte{0xAB});  // 16 unknown bytes
  h.header_bytes = 96;
  b.resize(EncodedSize(h));
  const ChunkHeader r = ReadHeader(b.data(), b.size());
  CHECK_EQ(r.header_bytes, 96u);
  CHECK_EQ(EncodedSize(r), 96u + 4u * 2u + 4u);
  // Writers produce 80-byte headers only.
  std::vector<std::byte> out(200);
  CHECK_THROWS(WriteHeader(r, out.data(), out.size()), std::invalid_argument, "80-byte");
}

TEST_CASE(ShapeAndDictionaryChecks) {
  const ChunkHeader h = MakeDataHeader(kShape, 10, Distribution::kTruncatedGaussian,
                                       2.5, Normalization::kAF, 38);
  CheckMatches(h, Kind::kData, kShape);
  CHECK_THROWS(CheckMatches(h, Kind::kWeights, kShape), FormatError, "holds data, expected weights");
  CHECK_THROWS(CheckMatches(h, Kind::kData, Shape{3, 702, 64, 4}), FormatError,
               "shape (3, 703, 64, 4) does not match the expected (3, 702, 64, 4)");
  CheckDictionary(h, DecodeTable(10, Distribution::kTruncatedGaussian, 2.5));
  CHECK_THROWS(CheckDictionary(h, DecodeTable(10, Distribution::kTruncatedGaussian, 3.0)),
               FormatError, "dictionary checksum mismatch");
}

TEST_CASE(WriterRejectsInvalidParameters) {
  CHECK_THROWS(MakeDataHeader(kShape, 5, Distribution::kGaussian, 0.0, Normalization::kRF, 0),
               std::invalid_argument, "unsupported bit count 5");
  CHECK_THROWS(MakeDataHeader(kShape, 10, Distribution::kTruncatedGaussian, 0.0,
                              Normalization::kRF, 0),
               std::invalid_argument, "truncation must be in (0, 8]");
  CHECK_THROWS(MakeDataHeader(kShape, 10, Distribution::kGaussian, 0.0, Normalization::kAF, 0),
               std::invalid_argument, "AF needs n_antenna");
  CHECK_THROWS(MakeDataHeader(Shape{0, 1, 1, 1}, 10, Distribution::kGaussian, 0.0,
                              Normalization::kRF, 0),
               std::invalid_argument, "zero-sized dimension");
  CHECK_THROWS(MakeDataHeader(Shape{1, std::size_t{1} << 33, 1, 1}, 10,
                              Distribution::kGaussian, 0.0, Normalization::kRF, 0),
               std::invalid_argument, "n_baseline exceeds 2^32-1");
  CHECK_THROWS(MakeDataHeader(Shape{1, 1u << 20, 1u << 12, 4}, 16, Distribution::kGaussian,
                              0.0, Normalization::kRow, 0),
               std::invalid_argument, "block too large");
  CHECK_THROWS(MakeDataHeader(kShape, 10, Distribution::kGaussian, 0.0, Normalization::kRF, 0,
                              kFlagAntennaMapEmbedded),
               std::invalid_argument, "only allowed for AF");
  std::vector<std::byte> small(10);
  const ChunkHeader h = MakeWeightsHeader(kShape, 12);
  CHECK_THROWS(WriteHeader(h, small.data(), small.size()), std::invalid_argument,
               "smaller than the header");
}

// Random single- and multi-byte corruptions must give either a valid header or
// a FormatError, never another exception or a crash (run under sanitizers in
// scripts/check.sh).
TEST_CASE(RandomCorruptionIsHandled) {
  const ChunkHeader h = MakeDataHeader({2, 6, 3, 2}, 6, Distribution::kGaussian, 0.0,
                                       Normalization::kAF, 4, kFlagAntennaMapEmbedded);
  const std::vector<std::byte> good = FullChunk(h);
  std::uint64_t state = 42;
  std::size_t accepted = 0, rejected = 0;
  for (int round = 0; round != 50000; ++round) {
    std::vector<std::byte> b = good;
    const int edits = 1 + static_cast<int>(Mix(++state) % 3);
    for (int e = 0; e != edits; ++e) {
      const std::size_t offset = Mix(++state) % kHeaderBytes;
      b[offset] = static_cast<std::byte>(Mix(++state) & 0xFF);
    }
    const std::size_t length = (Mix(++state) % 8 == 0) ? Mix(++state) % b.size() : b.size();
    try {
      (void)ReadHeader(b.data(), length);
      ++accepted;
    } catch (const FormatError&) {
      ++rejected;
    }
  }
  std::printf("  corruption rounds: %zu accepted, %zu rejected\n", accepted, rejected);
  CHECK(rejected > accepted);
}
