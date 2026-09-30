// The versioned chunk header (spec/SPEC.md section 5).
// SPDX-License-Identifier: GPL-3.0-only
//
// Convention notice: unofficial DP3Core conventions, not endorsed by LOFAR,
// ASTRON or the MSv4 schema group.
#ifndef DP3CORE_DYSCO_HEADER_H_
#define DP3CORE_DYSCO_HEADER_H_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "dp3core/dysco/types.h"

namespace dp3core::dysco {

/// Size of the header written by this library (format 1).
inline constexpr std::size_t kHeaderBytes = 80;

/// The 8 magic bytes, ASCII "DP3DYSCO".
inline constexpr char kMagic[8] = {'D', 'P', '3', 'D', 'Y', 'S', 'C', 'O'};

/// Header flag bits.
inline constexpr std::uint32_t kFlagDictionaryEmbedded = 1u << 0;
inline constexpr std::uint32_t kFlagAntennaMapEmbedded = 1u << 1;

/// Thrown when encoded bytes are not a valid chunk (or don't match what the
/// caller expects). Invalid encoder parameters throw std::invalid_argument.
class FormatError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// The decoded fields of a chunk header. See spec/SPEC.md table 5.1.
struct ChunkHeader {
  std::uint16_t format_version = 1;
  std::uint16_t header_bytes = kHeaderBytes;
  Kind kind = Kind::kData;
  std::uint8_t bits = 10;
  std::uint8_t distribution = 0;   ///< a Distribution code, or kNotApplicable
  std::uint8_t normalization = 0;  ///< a Normalization code, or kNotApplicable
  std::uint32_t flags = 0;
  std::uint32_t n_time = 0;
  std::uint32_t n_baseline = 0;
  std::uint32_t n_channel = 0;
  std::uint32_t n_pol = 0;
  std::uint32_t n_antenna = 0;
  double truncation = 0.0;
  std::uint64_t nonce = 0;
  std::uint64_t dict_checksum = 0;
  std::uint32_t side_floats_per_block = 0;
  std::uint32_t symbol_bytes_per_block = 0;

  Shape GetShape() const { return {n_time, n_baseline, n_channel, n_pol}; }
  Distribution GetDistribution() const { return static_cast<Distribution>(distribution); }
  Normalization GetNormalization() const { return static_cast<Normalization>(normalization); }
};

/// Builds a valid data header, including the derived sizes and the dictionary
/// checksum. @p n_antenna must be >= 1 for AF and is ignored otherwise.
/// @throws std::invalid_argument on invalid parameters.
ChunkHeader MakeDataHeader(const Shape& shape, unsigned bits,
                           Distribution distribution, double truncation,
                           Normalization normalization, std::uint32_t n_antenna,
                           std::uint32_t flags = 0, std::uint64_t nonce = 0);

/// Builds a valid weights header. @throws std::invalid_argument.
ChunkHeader MakeWeightsHeader(const Shape& shape, unsigned bits);

/// Side-data floats per block (M) and packed symbol bytes per block (Y),
/// computed from the other fields. @throws FormatError on overflow or when the
/// codes they depend on are invalid.
std::uint64_t ExpectedSideFloatsPerBlock(const ChunkHeader& header);
std::uint64_t ExpectedSymbolBytesPerBlock(const ChunkHeader& header);

/// Sizes of the optional sections, of one block, and of the whole chunk.
std::uint64_t DictionaryBytes(const ChunkHeader& header);
std::uint64_t AntennaMapBytes(const ChunkHeader& header);
std::uint64_t BlockBytes(const ChunkHeader& header);
std::uint64_t EncodedSize(const ChunkHeader& header);

/// Checks every rule of spec section 5.3 that the header alone determines.
/// @throws FormatError naming the first violated rule.
void ValidateHeader(const ChunkHeader& header);

/// Serializes a valid header with header_bytes == kHeaderBytes.
/// @throws std::invalid_argument if the header is invalid or @p out_size is too small.
void WriteHeader(const ChunkHeader& header, std::byte* out, std::size_t out_size);

/// Parses and validates a header. With @p require_complete_chunk, also checks
/// that @p size equals EncodedSize(); otherwise only the header must be present.
/// @throws FormatError.
ChunkHeader ReadHeader(const std::byte* in, std::size_t size,
                       bool require_complete_chunk = true);

/// Checks that the chunk holds @p kind with exactly @p shape. @throws FormatError.
void CheckMatches(const ChunkHeader& header, Kind kind, const Shape& shape);

/// Checks the header's dict_checksum against @p table. @throws FormatError.
void CheckDictionary(const ChunkHeader& header, const std::vector<float>& table);

}  // namespace dp3core::dysco

#endif
