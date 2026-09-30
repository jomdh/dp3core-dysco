// SPDX-License-Identifier: GPL-3.0-only
#include "dp3core/dysco/codec.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "dp3core/dysco/bitpack.h"
#include "dp3core/dysco/dither.h"
#include "dp3core/dysco/tables.h"
#include "normalize.h"
#include "weights.h"

namespace dp3core::dysco {
namespace {

void PutU32(std::byte* out, std::uint32_t v) {
  for (int k = 0; k != 4; ++k) out[k] = static_cast<std::byte>(v >> (8 * k));
}
std::uint32_t GetU32(const std::byte* in) {
  std::uint32_t v = 0;
  for (int k = 0; k != 4; ++k) v |= std::to_integer<std::uint32_t>(in[k]) << (8 * k);
  return v;
}
void PutFloat(std::byte* out, float f) {
  std::uint32_t u;
  std::memcpy(&u, &f, 4);
  PutU32(out, u);
}
float GetFloat(const std::byte* in) {
  const std::uint32_t u = GetU32(in);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

std::size_t ToSize(std::uint64_t v) {
  if (v > std::numeric_limits<std::size_t>::max())
    throw std::invalid_argument("encoded size exceeds size_t");
  return static_cast<std::size_t>(v);
}

std::uint32_t Flags(const DataParams& p) {
  std::uint32_t flags = 0;
  if (p.embed_dictionary) flags |= kFlagDictionaryEmbedded;
  if (p.embed_antenna_map) flags |= kFlagAntennaMapEmbedded;
  return flags;
}

void ValidateMap(const AntennaMap& map, std::size_t n_baseline) {
  if (map.n_antenna == 0) throw std::invalid_argument("antenna map: n_antenna is 0");
  if (map.antenna1.size() != n_baseline || map.antenna2.size() != n_baseline)
    throw std::invalid_argument("antenna map: " + std::to_string(map.antenna1.size()) +
                                "/" + std::to_string(map.antenna2.size()) +
                                " entries for " + std::to_string(n_baseline) + " baselines");
  for (std::size_t b = 0; b != n_baseline; ++b)
    if (map.antenna1[b] >= map.n_antenna || map.antenna2[b] >= map.n_antenna)
      throw std::invalid_argument("antenna map: baseline " + std::to_string(b) +
                                  " refers to an antenna >= n_antenna (" +
                                  std::to_string(map.n_antenna) + ")");
}

ChunkHeader DataHeader(const DataParams& p, const Shape& shape, const AntennaMap* map,
                       std::uint64_t nonce) {
  const bool af = p.normalization == Normalization::kAF;
  if (af) {
    if (!map) throw std::invalid_argument("AF normalization needs an antenna map");
    ValidateMap(*map, shape.n_baseline);
  }
  if (p.embed_antenna_map && !af)
    throw std::invalid_argument("embed_antenna_map is only allowed with AF");
  return MakeDataHeader(shape, p.bits, p.distribution, p.truncation, p.normalization,
                        af ? map->n_antenna : 0, Flags(p), nonce);
}

// Runs fn(t) for t in [0, n) on up to `threads` threads; rethrows the first
// exception. Each block is independent, so the result doesn't depend on it.
template <typename Fn>
void ForEachBlock(std::size_t n, unsigned threads, Fn fn) {
  const std::size_t n_threads = std::min<std::size_t>(threads == 0 ? 1 : threads, n);
  if (n_threads <= 1) {
    for (std::size_t t = 0; t != n; ++t) fn(t);
    return;
  }
  std::vector<std::thread> workers;
  std::vector<std::exception_ptr> errors(n_threads);
  for (std::size_t w = 0; w != n_threads; ++w) {
    workers.emplace_back([&, w]() {
      try {
        for (std::size_t t = w; t < n; t += n_threads) fn(t);
      } catch (...) {
        errors[w] = std::current_exception();
      }
    });
  }
  for (auto& worker : workers) worker.join();
  for (auto& e : errors)
    if (e) std::rethrow_exception(e);
}

}  // namespace

void Validate(const DataParams& params, const Shape& shape, const AntennaMap* map) {
  (void)DataHeader(params, shape, map, 0);
}

void Validate(const WeightParams& params, const Shape& shape) {
  (void)MakeWeightsHeader(shape, params.bits);
}

std::size_t EncodedSize(const DataParams& params, const Shape& shape,
                        const AntennaMap* map) {
  return ToSize(EncodedSize(DataHeader(params, shape, map, 0)));
}

std::size_t EncodedSize(const WeightParams& params, const Shape& shape) {
  return ToSize(EncodedSize(MakeWeightsHeader(shape, params.bits)));
}

void EncodeData(const DataParams& params, const Shape& shape, const AntennaMap* map,
                const std::complex<float>* in, std::byte* out, std::size_t out_size,
                const EncodeOptions& options) {
  (void)DataHeader(params, shape, map, 0);  // validate before hashing the input
  const std::size_t per_block = shape.n_baseline * shape.n_channel * shape.n_pol;
  const std::uint64_t nonce = ChunkNonce(in, shape.n_time * per_block);
  const ChunkHeader h = DataHeader(params, shape, map, nonce);
  const std::size_t size = ToSize(EncodedSize(h));
  if (out_size < size)
    throw std::invalid_argument("output buffer of " + std::to_string(out_size) +
                                " bytes; " + std::to_string(size) + " needed");
  const auto table = GetCachedTable(params.bits, params.distribution, params.truncation);
  const std::vector<float>& levels = table->levels;
  const std::size_t n_levels = levels.size();

  WriteHeader(h, out, out_size);
  std::byte* pos = out + h.header_bytes;
  if (h.flags & kFlagDictionaryEmbedded) {
    for (float level : levels) {
      PutFloat(pos, level);
      pos += 4;
    }
  }
  if (h.flags & kFlagAntennaMapEmbedded) {
    for (std::size_t b = 0; b != shape.n_baseline; ++b) {
      PutU32(pos, map->antenna1[b]);
      PutU32(pos + 4, map->antenna2[b]);
      pos += 8;
    }
  }
  std::byte* const blocks = pos;
  const std::size_t side_count = h.side_floats_per_block;
  const std::size_t block_bytes = ToSize(BlockBytes(h));

  detail::BlockLayout layout;
  layout.n_baseline = shape.n_baseline;
  layout.n_channel = shape.n_channel;
  layout.n_pol = shape.n_pol;
  if (params.normalization == Normalization::kAF) {
    layout.n_antenna = map->n_antenna;
    layout.antenna1 = map->antenna1.data();
    layout.antenna2 = map->antenna2.data();
  }
  const std::uint64_t key = DitherKey(params.seed, nonce);
  const double max_level = levels.back();

  ForEachBlock(shape.n_time, options.threads, [&](std::size_t t) {
    const std::complex<float>* block_in = in + t * per_block;
    std::vector<std::complex<double>> work(block_in, block_in + per_block);
    std::vector<float> side(side_count);
    switch (params.normalization) {
      case Normalization::kRow: detail::NormalizeRow(work.data(), layout, max_level, side.data()); break;
      case Normalization::kRF: detail::NormalizeRF(work.data(), layout, max_level, side.data()); break;
      case Normalization::kAF: detail::NormalizeAF(work.data(), layout, max_level, side.data()); break;
    }
    std::vector<std::uint32_t> symbols(2 * per_block);
    const std::uint64_t base = static_cast<std::uint64_t>(t) * per_block * 2;
    for (std::size_t i = 0; i != per_block; ++i) {
      symbols[2 * i] = QuantizeDithered(static_cast<float>(work[i].real()), levels.data(),
                                        n_levels, DitherValue(key, base + 2 * i));
      symbols[2 * i + 1] = QuantizeDithered(static_cast<float>(work[i].imag()), levels.data(),
                                            n_levels, DitherValue(key, base + 2 * i + 1));
    }
    std::byte* block_out = blocks + t * block_bytes;
    for (std::size_t k = 0; k != side_count; ++k) PutFloat(block_out + 4 * k, side[k]);
    PackSymbols(symbols.data(), symbols.size(), params.bits, block_out + 4 * side_count);
  });
}

void DecodeData(const std::byte* in, std::size_t in_size, const Shape& shape,
                const AntennaMap* map, std::complex<float>* out) {
  const ChunkHeader h = ReadHeader(in, in_size);
  CheckMatches(h, Kind::kData, shape);
  const std::byte* pos = in + h.header_bytes;

  std::shared_ptr<const CachedTable> cached;
  std::vector<float> levels;
  if (h.flags & kFlagDictionaryEmbedded) {
    const std::size_t n = (std::size_t{1} << h.bits) - 1;
    levels.resize(n);
    for (std::size_t i = 0; i != n; ++i) levels[i] = GetFloat(pos + 4 * i);
    pos += 4 * n;
    if (TableChecksum(levels) != h.dict_checksum)
      throw FormatError("dp3core.dysco chunk: embedded dictionary does not match dict_checksum");
  } else {
    cached = GetCachedTable(h.bits, h.GetDistribution(), h.truncation);
    CheckDictionary(h, cached->levels);
    levels = cached->levels;
  }
  levels.push_back(std::numeric_limits<float>::quiet_NaN());  // symbol Q-1

  const bool af = h.GetNormalization() == Normalization::kAF;
  AntennaMap embedded;
  const AntennaMap* use_map = map;
  if (h.flags & kFlagAntennaMapEmbedded) {
    embedded.n_antenna = h.n_antenna;
    embedded.antenna1.resize(h.n_baseline);
    embedded.antenna2.resize(h.n_baseline);
    for (std::size_t b = 0; b != h.n_baseline; ++b) {
      embedded.antenna1[b] = GetU32(pos);
      embedded.antenna2[b] = GetU32(pos + 4);
      pos += 8;
    }
    try {
      ValidateMap(embedded, h.n_baseline);
    } catch (const std::invalid_argument& e) {
      throw FormatError(std::string("dp3core.dysco chunk: embedded ") + e.what());
    }
    if (map && (map->n_antenna != embedded.n_antenna || map->antenna1 != embedded.antenna1 ||
                map->antenna2 != embedded.antenna2))
      throw FormatError("dp3core.dysco chunk: embedded antenna map differs from the given one");
    use_map = &embedded;
  } else if (af) {
    if (!map) throw std::invalid_argument("decoding an AF chunk needs an antenna map");
    ValidateMap(*map, h.n_baseline);
    if (map->n_antenna != h.n_antenna)
      throw FormatError("dp3core.dysco chunk: n_antenna " + std::to_string(h.n_antenna) +
                        " differs from the antenna map's " + std::to_string(map->n_antenna));
  }

  detail::BlockLayout layout;
  layout.n_baseline = h.n_baseline;
  layout.n_channel = h.n_channel;
  layout.n_pol = h.n_pol;
  if (af) {
    layout.n_antenna = use_map->n_antenna;
    layout.antenna1 = use_map->antenna1.data();
    layout.antenna2 = use_map->antenna2.data();
  }
  const std::size_t per_block = shape.n_baseline * shape.n_channel * shape.n_pol;
  const std::size_t side_count = h.side_floats_per_block;
  const std::size_t block_bytes = ToSize(BlockBytes(h));
  std::vector<float> side(side_count);
  std::vector<std::uint32_t> symbols(2 * per_block);
  for (std::size_t t = 0; t != shape.n_time; ++t) {
    const std::byte* block = pos + t * block_bytes;
    for (std::size_t k = 0; k != side_count; ++k) side[k] = GetFloat(block + 4 * k);
    UnpackSymbols(block + 4 * side_count, symbols.size(), h.bits, symbols.data());
    std::complex<float>* block_out = out + t * per_block;
    switch (h.GetNormalization()) {
      case Normalization::kRow: detail::DenormalizeRow(symbols.data(), levels.data(), layout, side.data(), block_out); break;
      case Normalization::kRF: detail::DenormalizeRF(symbols.data(), levels.data(), layout, side.data(), block_out); break;
      case Normalization::kAF: detail::DenormalizeAF(symbols.data(), levels.data(), layout, side.data(), block_out); break;
    }
  }
}

void EncodeWeights(const WeightParams& params, const Shape& shape, const float* in,
                   std::byte* out, std::size_t out_size, const EncodeOptions& options) {
  const ChunkHeader h = MakeWeightsHeader(shape, params.bits);
  const std::size_t size = ToSize(EncodedSize(h));
  if (out_size < size)
    throw std::invalid_argument("output buffer of " + std::to_string(out_size) +
                                " bytes; " + std::to_string(size) + " needed");
  const std::size_t per_block = shape.n_baseline * shape.n_channel * shape.n_pol;
  const std::size_t total = shape.n_time * per_block;
  for (std::size_t i = 0; i != total; ++i)
    if (!std::isfinite(in[i]) || in[i] < 0.0f)
      throw std::invalid_argument("dysco weights: value " + std::to_string(in[i]) +
                                  " at index " + std::to_string(i) +
                                  " is negative or not finite");
  WriteHeader(h, out, out_size);
  std::byte* const blocks = out + h.header_bytes;
  const std::size_t block_bytes = ToSize(BlockBytes(h));
  const std::size_t quant_count = std::size_t{1} << params.bits;
  ForEachBlock(shape.n_time, options.threads, [&](std::size_t t) {
    std::vector<std::uint32_t> symbols(shape.n_baseline * shape.n_channel);
    float side;
    detail::EncodeWeightBlock(in + t * per_block, shape.n_baseline, shape.n_channel,
                              shape.n_pol, quant_count, &side, symbols.data());
    std::byte* block_out = blocks + t * block_bytes;
    PutFloat(block_out, side);
    PackSymbols(symbols.data(), symbols.size(), params.bits, block_out + 4);
  });
}

void DecodeWeights(const std::byte* in, std::size_t in_size, const Shape& shape,
                   float* out) {
  const ChunkHeader h = ReadHeader(in, in_size);
  CheckMatches(h, Kind::kWeights, shape);
  const std::byte* const blocks = in + h.header_bytes;
  const std::size_t block_bytes = ToSize(BlockBytes(h));
  const std::size_t per_block = shape.n_baseline * shape.n_channel * shape.n_pol;
  const std::size_t quant_count = std::size_t{1} << h.bits;
  std::vector<std::uint32_t> symbols(shape.n_baseline * shape.n_channel);
  for (std::size_t t = 0; t != shape.n_time; ++t) {
    const std::byte* block = blocks + t * block_bytes;
    const float side = GetFloat(block);
    UnpackSymbols(block + 4, symbols.size(), h.bits, symbols.data());
    detail::DecodeWeightBlock(symbols.data(), shape.n_baseline, shape.n_channel,
                              shape.n_pol, quant_count, side, out + t * per_block);
  }
}

}  // namespace dp3core::dysco
