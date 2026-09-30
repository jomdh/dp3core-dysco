// Compares this port with the original Dysco sources, compiled from a local
// checkout of aroffringa/dysco (CMake: -DDP3CORE_DYSCO_REFERENCE_DIR=<path>).
// Only Dysco's casacore-free parts are used. GPL-3.0, like this library.
// SPDX-License-Identifier: GPL-3.0-only
#include <cstring>
#include <bytepacker.h>

#include <cmath>

#include <cstdint>
#include <vector>

#include "check.h"
#include "dp3core/dysco/bitpack.h"

using namespace dp3core::dysco;

namespace {
std::uint64_t Mix(std::uint64_t z) {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
}  // namespace

TEST_CASE(PackingEqualsDyscoBytePacker) {
  const unsigned widths[] = {2, 3, 4, 6, 8, 10, 12, 16};
  std::uint64_t state = 99;
  for (unsigned bits : widths) {
    for (std::size_t count = 0; count <= 40; ++count) {
      std::vector<unsigned> in(count);
      std::vector<std::uint32_t> in32(count);
      for (std::size_t i = 0; i != count; ++i)
        in32[i] = in[i] = static_cast<unsigned>(Mix(++state) & ((1u << bits) - 1));
      const std::size_t n = dyscostman::BytePacker::bufferSize(count, bits);
      CHECK_EQ(n, PackedBytes(count, bits));
      std::vector<unsigned char> theirs(n + 8, 0);
      std::vector<std::byte> ours(n + 8, std::byte{0});
      dyscostman::BytePacker::pack(bits, theirs.data(), in.data(), count);
      PackSymbols(in32.data(), count, bits, ours.data());
      bool same = true;
      for (std::size_t i = 0; i != n; ++i)
        same = same && std::to_integer<unsigned>(ours[i]) == theirs[i];
      if (!same) {
        std::cerr << "bits " << bits << " count " << count << '\n';
        CHECK(same);
      }
    }
  }
}

#include <stochasticencoder.h>

#include "dp3core/dysco/dither.h"

TEST_CASE(QuantizerEqualsDyscoEncodeWithDithering) {
  // Dysco's own table (GSL) and quantizer versus QuantizeDithered on the same
  // table: the quantizer logic must agree for every input and dither.
  const std::size_t quant_counts[] = {4, 16, 256, 4096};
  std::uint64_t state = 5;
  for (std::size_t q : quant_counts) {
    const auto encoder =
        dyscostman::StochasticEncoder<float>::TruncatedGausEncoder(q, 2.5, 1.0);
    std::vector<float> levels(q - 1);
    for (std::size_t s = 0; s != q - 1; ++s)
      levels[s] = encoder.Decode(static_cast<unsigned>(s));
    std::size_t mismatches = 0;
    for (int i = 0; i != 200000; ++i) {
      const std::uint64_t r = Mix64(++state);
      float v;
      if (i % 50 == 0) {
        v = levels[r % levels.size()];  // exactly on a level
      } else {
        v = static_cast<float>((double(r >> 11) / double(1ull << 53) - 0.5) * 7.0);
      }
      if (i % 997 == 0) v = std::nanf("");
      const std::uint32_t dither = static_cast<std::uint32_t>(Mix64(r) >> 33);
      const unsigned theirs = encoder.EncodeWithDithering(v, dither);
      const std::uint32_t ours = QuantizeDithered(v, levels.data(), levels.size(), dither);
      if (theirs != ours) ++mismatches;
    }
    CHECK_EQ(mismatches, std::size_t{0});
  }
}

#include <cstring>
// Dysco's timeblockbuffer.h uses memcpy without including <cstring>.
#include <aftimeblockencoder.h>
#include <rftimeblockencoder.h>
#include <rowtimeblockencoder.h>

#include <memory>
#include <random>

#include "normalize.h"

namespace {

bool SameFloat(float a, float b) {
  if (std::isnan(a) && std::isnan(b)) return true;
  std::uint32_t x, y;
  std::memcpy(&x, &a, 4);
  std::memcpy(&y, &b, 4);
  return x == y;
}

struct RefBlock {
  std::size_t n_ant, n_chan, n_pol;
  std::vector<std::uint32_t> a1, a2;
  std::vector<std::complex<float>> data;  // (baseline, channel, pol)
};

enum class Fill { kNormal, kZero, kNaN };

RefBlock MakeBlock(std::size_t n_ant, std::size_t n_chan, std::size_t n_pol,
                   bool autos, std::size_t missing_antenna, Fill fill,
                   std::uint64_t seed) {
  RefBlock b{n_ant, n_chan, n_pol, {}, {}, {}};
  for (std::uint32_t i = 0; i != n_ant; ++i)
    for (std::uint32_t j = autos ? i : i + 1; j < n_ant; ++j) {
      if (i == missing_antenna || j == missing_antenna) continue;
      b.a1.push_back(i);
      b.a2.push_back(j);
    }
  std::uint64_t s = seed;
  auto uniform = [&s]() { return double(Mix64(++s) >> 11) / double(1ull << 53); };
  auto gauss = [&]() {
    const double u = uniform() + 1e-300, v = uniform();
    return std::sqrt(-2.0 * std::log(u)) * std::cos(6.283185307179586 * v);
  };
  std::vector<double> gain(n_ant), chan(n_chan * n_pol);
  for (auto& g : gain) g = 0.2 + 3.0 * uniform();
  for (auto& c : chan) c = 0.5 + uniform();
  for (std::size_t r = 0; r != b.a1.size(); ++r)
    for (std::size_t ch = 0; ch != n_chan; ++ch)
      for (std::size_t p = 0; p != n_pol; ++p) {
        std::complex<float> v;
        if (fill == Fill::kZero) {
          v = 0.0f;
        } else if (fill == Fill::kNaN) {
          v = {std::nanf(""), 1.0f};
        } else {
          const double scale = gain[b.a1[r]] * gain[b.a2[r]] * chan[ch * n_pol + p] *
                               (b.a1[r] == b.a2[r] ? 30.0 : 1.0);
          v = {float(scale * gauss()), float(scale * gauss())};
          if (Mix64(s + 77) % 211 == 0) v = {std::nanf(""), 0.0f};
          if (Mix64(s + 78) % 307 == 0) v = {1.0f, std::numeric_limits<float>::infinity()};
          if (Mix64(s + 79) % 401 == 0) v = 0.0f;
        }
        b.data.push_back(v);
      }
  return b;
}

enum class Norm { kRow, kRF, kAF };

// Encodes the block with Dysco and with this port (same table, same
// mt19937 dither stream) and compares side data, symbols and decoded values.
void CompareWithDysco(const RefBlock& b, Norm norm, std::size_t quant_count,
                      unsigned rng_seed) {
  const auto encoder =
      dyscostman::StochasticEncoder<float>::TruncatedGausEncoder(quant_count, 2.5, 1.0);
  std::vector<float> levels(quant_count);
  for (std::size_t s = 0; s != quant_count - 1; ++s)
    levels[s] = encoder.Decode(static_cast<unsigned>(s));
  levels[quant_count - 1] = std::nanf("");
  const std::size_t n_levels = quant_count - 1;

  const std::size_t n_row = b.a1.size();
  const std::size_t vis_per_row = b.n_chan * b.n_pol;
  std::unique_ptr<TimeBlockEncoder> theirs;
  switch (norm) {
    case Norm::kRow: theirs = std::make_unique<RowTimeBlockEncoder>(b.n_pol, b.n_chan); break;
    case Norm::kRF: theirs = std::make_unique<RFTimeBlockEncoder>(b.n_pol, b.n_chan); break;
    case Norm::kAF: theirs = std::make_unique<AFTimeBlockEncoder>(b.n_pol, b.n_chan, true); break;
  }
  TimeBlockBuffer<std::complex<float>> buffer(b.n_pol, b.n_chan);
  for (std::size_t r = 0; r != n_row; ++r)
    buffer.SetData(r, b.a1[r], b.a2[r], &b.data[r * vis_per_row]);
  const std::size_t n_meta = theirs->MetaDataCount(n_row, b.n_pol, b.n_chan, b.n_ant);
  std::vector<float> their_meta(n_meta, -1.0f);
  std::vector<unsigned> their_symbols(n_row * vis_per_row * 2);
  std::mt19937 rnd(rng_seed);
  theirs->EncodeWithDithering(encoder, buffer, their_meta.data(), their_symbols.data(),
                              b.n_ant, rnd);

  detail::BlockLayout layout;
  layout.n_baseline = n_row;
  layout.n_channel = b.n_chan;
  layout.n_pol = b.n_pol;
  layout.n_antenna = b.n_ant;
  layout.antenna1 = b.a1.data();
  layout.antenna2 = b.a2.data();
  std::vector<std::complex<double>> work(b.data.begin(), b.data.end());
  std::vector<float> our_meta(n_meta, -2.0f);
  const double max_level = levels[n_levels - 1];
  switch (norm) {
    case Norm::kRow: detail::NormalizeRow(work.data(), layout, max_level, our_meta.data()); break;
    case Norm::kRF: detail::NormalizeRF(work.data(), layout, max_level, our_meta.data()); break;
    case Norm::kAF: detail::NormalizeAF(work.data(), layout, max_level, our_meta.data()); break;
  }
  std::mt19937 rnd2(rng_seed);
  auto dist = dyscostman::StochasticEncoder<float>::GetDitherDistribution();
  std::vector<std::uint32_t> our_symbols(their_symbols.size());
  for (std::size_t i = 0; i != work.size(); ++i) {
    our_symbols[2 * i] = QuantizeDithered(float(work[i].real()), levels.data(), n_levels, dist(rnd2));
    our_symbols[2 * i + 1] = QuantizeDithered(float(work[i].imag()), levels.data(), n_levels, dist(rnd2));
  }

  std::size_t meta_diff = 0, symbol_diff = 0, decode_diff = 0;
  for (std::size_t i = 0; i != n_meta; ++i) meta_diff += !SameFloat(our_meta[i], their_meta[i]);
  for (std::size_t i = 0; i != our_symbols.size(); ++i) symbol_diff += our_symbols[i] != their_symbols[i];

  // Decode Dysco's symbols with both decoders.
  theirs->InitializeDecode(their_meta.data(), n_row, b.n_ant);
  TimeBlockBuffer<std::complex<float>> out(b.n_pol, b.n_chan);
  out.resize(n_row);
  for (std::size_t r = 0; r != n_row; ++r)
    theirs->Decode(encoder, out, their_symbols.data(), r, b.a1[r], b.a2[r]);
  std::vector<std::complex<float>> ours(n_row * vis_per_row);
  switch (norm) {
    case Norm::kRow: detail::DenormalizeRow(their_symbols.data(), levels.data(), layout, their_meta.data(), ours.data()); break;
    case Norm::kRF: detail::DenormalizeRF(their_symbols.data(), levels.data(), layout, their_meta.data(), ours.data()); break;
    case Norm::kAF: detail::DenormalizeAF(their_symbols.data(), levels.data(), layout, their_meta.data(), ours.data()); break;
  }
  for (std::size_t r = 0; r != n_row; ++r)
    for (std::size_t i = 0; i != vis_per_row; ++i) {
      const auto t = out[r].visibilities[i];
      const auto o = ours[r * vis_per_row + i];
      decode_diff += !SameFloat(t.real(), o.real()) || !SameFloat(t.imag(), o.imag());
    }
  if (meta_diff || symbol_diff || decode_diff)
    std::cerr << "norm " << int(norm) << " Q " << quant_count << " rows " << n_row
              << ": side " << meta_diff << "/" << n_meta << ", symbols " << symbol_diff
              << ", decoded " << decode_diff << '\n';
  CHECK_EQ(meta_diff, std::size_t{0});
  CHECK_EQ(symbol_diff, std::size_t{0});
  CHECK_EQ(decode_diff, std::size_t{0});
}

void CompareAllNorms(Norm norm) {
  const std::size_t qs[] = {4, 64, 1024, 65536};
  unsigned seed = 1;
  for (std::size_t q : qs) {
    CompareWithDysco(MakeBlock(8, 7, 4, true, 99, Fill::kNormal, seed), norm, q, seed); ++seed;
    CompareWithDysco(MakeBlock(8, 7, 4, false, 99, Fill::kNormal, seed), norm, q, seed); ++seed;
    CompareWithDysco(MakeBlock(10, 3, 2, true, 4, Fill::kNormal, seed), norm, q, seed); ++seed;
    CompareWithDysco(MakeBlock(5, 1, 1, false, 99, Fill::kNormal, seed), norm, q, seed); ++seed;
    CompareWithDysco(MakeBlock(2, 16, 4, false, 99, Fill::kNormal, seed), norm, q, seed); ++seed;
    CompareWithDysco(MakeBlock(6, 4, 4, true, 99, Fill::kZero, seed), norm, q, seed); ++seed;
    CompareWithDysco(MakeBlock(6, 4, 4, true, 99, Fill::kNaN, seed), norm, q, seed); ++seed;
  }
  // A LOFAR-like block: 38 stations with autos, 16 channels, 4 pols.
  CompareWithDysco(MakeBlock(38, 16, 4, true, 99, Fill::kNormal, 1000), norm, 1024, 1000);
}

}  // namespace

TEST_CASE(RowEqualsDysco) { CompareAllNorms(Norm::kRow); }
TEST_CASE(RfEqualsDysco) { CompareAllNorms(Norm::kRF); }
TEST_CASE(AfEqualsDysco) { CompareAllNorms(Norm::kAF); }

#include <weightblockencoder.h>

#include "weights.h"

TEST_CASE(WeightsEqualDysco) {
  std::uint64_t s = 11;
  const std::size_t shapes[][3] = {{1, 1, 1}, {7, 5, 4}, {30, 16, 2}, {3, 64, 1}};
  const std::size_t qs[] = {4, 256, 1024, 65536};
  std::size_t mismatches = 0;
  for (const auto& sh : shapes) {
    for (std::size_t q : qs) {
      for (int variant = 0; variant != 3; ++variant) {
        const std::size_t nb = sh[0], nc = sh[1], np = sh[2];
        std::vector<float> w(nb * nc * np);
        for (auto& x : w) {
          const std::uint64_t r = Mix64(++s);
          x = variant == 0 ? 0.0f
              : variant == 1 ? float(double(r >> 11) / double(1ull << 53) * 1e4)
                             : float((r >> 40) % 5);  // many ties and zeros
        }
        TimeBlockBuffer<float> buffer(np, nc);
        for (std::size_t b = 0; b != nb; ++b) buffer.SetData(b, 0, 1, &w[b * nc * np]);
        ::WeightBlockEncoder theirs(np, nc, q);
        float their_side = -1, our_side = -2;
        std::vector<unsigned> their_sym(nb * nc);
        std::vector<std::uint32_t> our_sym(nb * nc);
        theirs.Encode(buffer, &their_side, their_sym.data());
        detail::EncodeWeightBlock(w.data(), nb, nc, np, q, &our_side, our_sym.data());
        mismatches += !SameFloat(their_side, our_side);
        for (std::size_t i = 0; i != our_sym.size(); ++i) mismatches += their_sym[i] != our_sym[i];
        theirs.InitializeDecode(&their_side);
        TimeBlockBuffer<float> out(np, nc);
        out.resize(nb);
        for (std::size_t b = 0; b != nb; ++b) theirs.Decode(out, their_sym.data(), b);
        std::vector<float> ours(w.size());
        detail::DecodeWeightBlock(their_sym.data(), nb, nc, np, q, their_side, ours.data());
        for (std::size_t b = 0; b != nb; ++b)
          for (std::size_t i = 0; i != nc * np; ++i)
            mismatches += !SameFloat(out[b].visibilities[i], ours[b * nc * np + i]);
      }
    }
  }
  CHECK_EQ(mismatches, std::size_t{0});
}
