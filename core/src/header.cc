// The versioned chunk header (spec/SPEC.md section 5).
// SPDX-License-Identifier: GPL-3.0-only
#include "dp3core/dysco/header.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>

#include "dp3core/dysco/tables.h"
#include "dp3core/dysco/version.h"

namespace dp3core::dysco {
namespace {

[[noreturn]] void Fail(const std::string& message) {
  throw FormatError("dp3core.dysco chunk: " + message);
}

std::uint64_t CheckedMul(std::uint64_t a, std::uint64_t b, const char* what) {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a)
    Fail(std::string(what) + " overflows");
  return a * b;
}

std::uint64_t CheckedAdd(std::uint64_t a, std::uint64_t b, const char* what) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    Fail(std::string(what) + " overflows");
  return a + b;
}

bool IsPositiveZero(double x) {
  std::uint64_t u;
  std::memcpy(&u, &x, 8);
  return u == 0;
}

// Little-endian field access.
template <typename T>
void Put(std::byte* out, std::size_t offset, T value) {
  for (std::size_t i = 0; i != sizeof(T); ++i)
    out[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
}

template <typename T>
T Get(const std::byte* in, std::size_t offset) {
  T value = 0;
  for (std::size_t i = 0; i != sizeof(T); ++i)
    value = static_cast<T>(value | (static_cast<T>(std::to_integer<unsigned>(in[offset + i])) << (8 * i)));
  return value;
}

void PutDouble(std::byte* out, std::size_t offset, double value) {
  std::uint64_t u;
  std::memcpy(&u, &value, 8);
  Put<std::uint64_t>(out, offset, u);
}

double GetDouble(const std::byte* in, std::size_t offset) {
  const std::uint64_t u = Get<std::uint64_t>(in, offset);
  double value;
  std::memcpy(&value, &u, 8);
  return value;
}

std::uint32_t ToU32(std::size_t value, const char* what) {
  if (value > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument(std::string(what) + " exceeds 2^32-1");
  return static_cast<std::uint32_t>(value);
}

// Runs the header validation and reports failures as invalid parameters.
ChunkHeader Finish(ChunkHeader header) {
  const std::uint64_t m = ExpectedSideFloatsPerBlock(header);
  const std::uint64_t y = ExpectedSymbolBytesPerBlock(header);
  if (m > std::numeric_limits<std::uint32_t>::max() ||
      y > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument("block too large: side data or symbols exceed 2^32-1");
  header.side_floats_per_block = static_cast<std::uint32_t>(m);
  header.symbol_bytes_per_block = static_cast<std::uint32_t>(y);
  try {
    ValidateHeader(header);
  } catch (const FormatError& e) {
    throw std::invalid_argument(e.what());
  }
  return header;
}

}  // namespace

ChunkHeader MakeDataHeader(const Shape& shape, unsigned bits,
                           Distribution distribution, double truncation,
                           Normalization normalization, std::uint32_t n_antenna,
                           std::uint32_t flags, std::uint64_t nonce) {
  if (!IsSupportedBitCount(bits))
    throw std::invalid_argument("unsupported bit count " + std::to_string(bits));
  ChunkHeader h;
  h.kind = Kind::kData;
  h.bits = static_cast<std::uint8_t>(bits);
  h.distribution = static_cast<std::uint8_t>(distribution);
  h.normalization = static_cast<std::uint8_t>(normalization);
  h.flags = flags;
  h.n_time = ToU32(shape.n_time, "n_time");
  h.n_baseline = ToU32(shape.n_baseline, "n_baseline");
  h.n_channel = ToU32(shape.n_channel, "n_channel");
  h.n_pol = ToU32(shape.n_pol, "n_pol");
  h.n_antenna = normalization == Normalization::kAF ? n_antenna : 0;
  h.truncation = distribution == Distribution::kTruncatedGaussian ? truncation : 0.0;
  h.nonce = nonce;
  // Validate the cheap fields before building the table.
  (void)Finish(h);
  h.dict_checksum = TableChecksum(DecodeTable(bits, distribution, h.truncation));
  return Finish(h);
}

ChunkHeader MakeWeightsHeader(const Shape& shape, unsigned bits) {
  if (!IsSupportedBitCount(bits))
    throw std::invalid_argument("unsupported bit count " + std::to_string(bits));
  ChunkHeader h;
  h.kind = Kind::kWeights;
  h.bits = static_cast<std::uint8_t>(bits);
  h.distribution = kNotApplicable;
  h.normalization = kNotApplicable;
  h.n_time = ToU32(shape.n_time, "n_time");
  h.n_baseline = ToU32(shape.n_baseline, "n_baseline");
  h.n_channel = ToU32(shape.n_channel, "n_channel");
  h.n_pol = ToU32(shape.n_pol, "n_pol");
  return Finish(h);
}

std::uint64_t ExpectedSideFloatsPerBlock(const ChunkHeader& h) {
  const std::uint64_t b = h.n_baseline, f = h.n_channel, p = h.n_pol,
                      a = h.n_antenna;
  if (h.kind == Kind::kWeights) return 1;
  if (h.kind != Kind::kData) Fail("invalid kind");
  switch (h.normalization) {
    case static_cast<std::uint8_t>(Normalization::kAF):
      return CheckedMul(p, f + a, "side data size");
    case static_cast<std::uint8_t>(Normalization::kRF):
      return CheckedMul(p, f + b, "side data size");
    case static_cast<std::uint8_t>(Normalization::kRow):
      return b;
    default:
      Fail("invalid normalization code " + std::to_string(h.normalization));
  }
}

std::uint64_t ExpectedSymbolBytesPerBlock(const ChunkHeader& h) {
  std::uint64_t symbols = CheckedMul(h.n_baseline, h.n_channel, "symbol count");
  if (h.kind == Kind::kData) {
    symbols = CheckedMul(symbols, h.n_pol, "symbol count");
    symbols = CheckedMul(symbols, 2, "symbol count");
  } else if (h.kind != Kind::kWeights) {
    Fail("invalid kind");
  }
  const std::uint64_t bits = CheckedMul(symbols, h.bits, "symbol bit count");
  return bits / 8 + (bits % 8 != 0 ? 1 : 0);
}

std::uint64_t DictionaryBytes(const ChunkHeader& h) {
  if (!(h.flags & kFlagDictionaryEmbedded)) return 0;
  return 4 * ((std::uint64_t{1} << h.bits) - 1);
}

std::uint64_t AntennaMapBytes(const ChunkHeader& h) {
  if (!(h.flags & kFlagAntennaMapEmbedded)) return 0;
  return CheckedMul(8, h.n_baseline, "antenna map size");
}

std::uint64_t BlockBytes(const ChunkHeader& h) {
  return CheckedAdd(CheckedMul(4, h.side_floats_per_block, "block size"),
                    h.symbol_bytes_per_block, "block size");
}

std::uint64_t EncodedSize(const ChunkHeader& h) {
  std::uint64_t size = h.header_bytes;
  size = CheckedAdd(size, DictionaryBytes(h), "chunk size");
  size = CheckedAdd(size, AntennaMapBytes(h), "chunk size");
  return CheckedAdd(size, CheckedMul(h.n_time, BlockBytes(h), "chunk size"),
                    "chunk size");
}

void ValidateHeader(const ChunkHeader& h) {
  if (h.format_version == 0 || h.format_version > kFormatVersion)
    Fail("unsupported format_version " + std::to_string(h.format_version) +
         " (this library reads up to " + std::to_string(kFormatVersion) + ")");
  if (h.header_bytes < kHeaderBytes)
    Fail("header_bytes " + std::to_string(h.header_bytes) + " < 80");
  const bool is_data = h.kind == Kind::kData;
  if (!is_data && h.kind != Kind::kWeights)
    Fail("invalid kind code " + std::to_string(static_cast<unsigned>(h.kind)));
  if (!IsSupportedBitCount(h.bits))
    Fail("unsupported bits " + std::to_string(h.bits));
  if (is_data) {
    if (h.distribution > 2)
      Fail("invalid distribution code " + std::to_string(h.distribution));
    if (h.normalization > 2)
      Fail("invalid normalization code " + std::to_string(h.normalization));
  } else {
    if (h.distribution != kNotApplicable || h.normalization != kNotApplicable)
      Fail("weights chunk must have distribution and normalization 255");
  }
  const bool is_af = is_data && h.normalization == static_cast<std::uint8_t>(Normalization::kAF);
  if (h.flags & ~(kFlagDictionaryEmbedded | kFlagAntennaMapEmbedded))
    Fail("unknown flag bits");
  if ((h.flags & kFlagDictionaryEmbedded) && !is_data)
    Fail("embedded dictionary is only allowed for data");
  if ((h.flags & kFlagAntennaMapEmbedded) && !is_af)
    Fail("embedded antenna map is only allowed for AF data");
  if (h.n_time == 0 || h.n_baseline == 0 || h.n_channel == 0 || h.n_pol == 0)
    Fail("zero-sized dimension");
  if (is_af ? h.n_antenna == 0 : h.n_antenna != 0)
    Fail(is_af ? "AF needs n_antenna >= 1" : "n_antenna must be 0 unless AF");
  const bool is_truncated =
      is_data && h.distribution == static_cast<std::uint8_t>(Distribution::kTruncatedGaussian);
  if (is_truncated) {
    if (!(h.truncation > 0.0 && h.truncation <= kMaxTruncation))
      Fail("truncation must be in (0, 8]");
  } else if (!IsPositiveZero(h.truncation)) {
    Fail("truncation must be +0.0 unless truncated_gaussian");
  }
  if (!is_data && h.dict_checksum != 0) Fail("weights chunk must have dict_checksum 0");
  if (h.side_floats_per_block != ExpectedSideFloatsPerBlock(h))
    Fail("side_floats_per_block does not match the shape");
  if (h.symbol_bytes_per_block != ExpectedSymbolBytesPerBlock(h))
    Fail("symbol_bytes_per_block does not match the shape");
}

void WriteHeader(const ChunkHeader& h, std::byte* out, std::size_t out_size) {
  if (h.header_bytes != kHeaderBytes)
    throw std::invalid_argument("this library writes 80-byte headers only");
  try {
    ValidateHeader(h);
  } catch (const FormatError& e) {
    throw std::invalid_argument(e.what());
  }
  if (out_size < kHeaderBytes) throw std::invalid_argument("output buffer smaller than the header");
  std::memcpy(out, kMagic, 8);
  Put<std::uint16_t>(out, 8, h.format_version);
  Put<std::uint16_t>(out, 10, h.header_bytes);
  Put<std::uint8_t>(out, 12, static_cast<std::uint8_t>(h.kind));
  Put<std::uint8_t>(out, 13, h.bits);
  Put<std::uint8_t>(out, 14, h.distribution);
  Put<std::uint8_t>(out, 15, h.normalization);
  Put<std::uint32_t>(out, 16, h.flags);
  Put<std::uint32_t>(out, 20, h.n_time);
  Put<std::uint32_t>(out, 24, h.n_baseline);
  Put<std::uint32_t>(out, 28, h.n_channel);
  Put<std::uint32_t>(out, 32, h.n_pol);
  Put<std::uint32_t>(out, 36, h.n_antenna);
  PutDouble(out, 40, h.truncation);
  Put<std::uint64_t>(out, 48, h.nonce);
  Put<std::uint64_t>(out, 56, h.dict_checksum);
  Put<std::uint32_t>(out, 64, h.side_floats_per_block);
  Put<std::uint32_t>(out, 68, h.symbol_bytes_per_block);
  Put<std::uint64_t>(out, 72, 0);
}

ChunkHeader ReadHeader(const std::byte* in, std::size_t size,
                       bool require_complete_chunk) {
  if (size < 12) Fail("buffer of " + std::to_string(size) + " bytes is too short for a header");
  if (std::memcmp(in, kMagic, 8) != 0) Fail("bad magic (not a dp3core.dysco chunk)");
  ChunkHeader h;
  h.format_version = Get<std::uint16_t>(in, 8);
  h.header_bytes = Get<std::uint16_t>(in, 10);
  if (h.format_version == 0 || h.format_version > kFormatVersion)
    Fail("unsupported format_version " + std::to_string(h.format_version) +
         " (this library reads up to " + std::to_string(kFormatVersion) + ")");
  if (h.header_bytes < kHeaderBytes)
    Fail("header_bytes " + std::to_string(h.header_bytes) + " < 80");
  if (size < h.header_bytes)
    Fail("buffer of " + std::to_string(size) + " bytes is shorter than the " +
         std::to_string(h.header_bytes) + "-byte header");
  h.kind = static_cast<Kind>(Get<std::uint8_t>(in, 12));
  h.bits = Get<std::uint8_t>(in, 13);
  h.distribution = Get<std::uint8_t>(in, 14);
  h.normalization = Get<std::uint8_t>(in, 15);
  h.flags = Get<std::uint32_t>(in, 16);
  h.n_time = Get<std::uint32_t>(in, 20);
  h.n_baseline = Get<std::uint32_t>(in, 24);
  h.n_channel = Get<std::uint32_t>(in, 28);
  h.n_pol = Get<std::uint32_t>(in, 32);
  h.n_antenna = Get<std::uint32_t>(in, 36);
  h.truncation = GetDouble(in, 40);
  h.nonce = Get<std::uint64_t>(in, 48);
  h.dict_checksum = Get<std::uint64_t>(in, 56);
  h.side_floats_per_block = Get<std::uint32_t>(in, 64);
  h.symbol_bytes_per_block = Get<std::uint32_t>(in, 68);
  if (Get<std::uint64_t>(in, 72) != 0) Fail("reserved field is not 0");
  ValidateHeader(h);
  if (require_complete_chunk) {
    const std::uint64_t expected = EncodedSize(h);
    if (size != expected)
      Fail("chunk is " + std::to_string(size) + " bytes, header implies " +
           std::to_string(expected));
  }
  return h;
}

void CheckMatches(const ChunkHeader& h, Kind kind, const Shape& shape) {
  if (h.kind != kind)
    Fail(std::string("holds ") + ToString(h.kind) + ", expected " + ToString(kind));
  if (h.n_time != shape.n_time || h.n_baseline != shape.n_baseline ||
      h.n_channel != shape.n_channel || h.n_pol != shape.n_pol) {
    std::ostringstream s;
    s << "shape (" << h.n_time << ", " << h.n_baseline << ", " << h.n_channel
      << ", " << h.n_pol << ") does not match the expected (" << shape.n_time
      << ", " << shape.n_baseline << ", " << shape.n_channel << ", "
      << shape.n_pol << ")";
    Fail(s.str());
  }
}

void CheckDictionary(const ChunkHeader& h, const std::vector<float>& table) {
  const std::uint64_t got = TableChecksum(table);
  if (got != h.dict_checksum) {
    std::ostringstream s;
    s << std::hex << "dictionary checksum mismatch: header 0x" << h.dict_checksum
      << ", table 0x" << got << " (an implementation bug, not bad data)";
    Fail(s.str());
  }
}

}  // namespace dp3core::dysco
