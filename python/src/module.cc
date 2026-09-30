// pybind11 binding of the core chunk API (design 7.2).
// SPDX-License-Identifier: GPL-3.0-only
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <complex>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "dp3core/dysco/codec.h"
#include "dp3core/dysco/dither.h"
#include "dp3core/dysco/header.h"
#include "dp3core/dysco/tables.h"
#include "dp3core/dysco/version.h"

namespace py = pybind11;
using namespace dp3core::dysco;

namespace {

Distribution ParseDistribution(const std::string& s) {
  if (s == "gaussian") return Distribution::kGaussian;
  if (s == "truncated_gaussian") return Distribution::kTruncatedGaussian;
  if (s == "uniform") return Distribution::kUniform;
  throw std::invalid_argument("unknown distribution '" + s +
                              "' (gaussian, truncated_gaussian, uniform)");
}

Normalization ParseNormalization(const std::string& s) {
  if (s == "af") return Normalization::kAF;
  if (s == "rf") return Normalization::kRF;
  if (s == "row") return Normalization::kRow;
  throw std::invalid_argument("unknown normalization '" + s + "' (af, rf, row)");
}

Shape ToShape(const std::vector<std::size_t>& s) {
  if (s.size() != 4)
    throw std::invalid_argument("expected a 4-dimensional (time, baseline, frequency, "
                                "polarization) shape, got " + std::to_string(s.size()) +
                                " dimensions");
  return {s[0], s[1], s[2], s[3]};
}

std::optional<AntennaMap> MakeMap(const std::optional<std::vector<std::uint32_t>>& a1,
                                  const std::optional<std::vector<std::uint32_t>>& a2,
                                  std::uint32_t n_antennas) {
  if (!a1 && !a2) return std::nullopt;
  if (!a1 || !a2) throw std::invalid_argument("antenna1 and antenna2 must be given together");
  AntennaMap m;
  m.n_antenna = n_antennas;
  m.antenna1 = *a1;
  m.antenna2 = *a2;
  return m;
}

DataParams MakeParams(unsigned bits, const std::string& distribution, double truncation,
                      const std::string& normalization, std::uint64_t seed,
                      bool embed_dictionary, bool embed_antenna_map) {
  DataParams p;
  p.bits = bits;
  p.distribution = ParseDistribution(distribution);
  p.truncation = truncation;
  p.normalization = ParseNormalization(normalization);
  p.seed = seed;
  p.embed_dictionary = embed_dictionary;
  p.embed_antenna_map = embed_antenna_map;
  return p;
}

std::vector<std::size_t> ShapeOf(const py::array& a) {
  std::vector<std::size_t> s;
  for (py::ssize_t i = 0; i != a.ndim(); ++i) s.push_back(static_cast<std::size_t>(a.shape(i)));
  return s;
}

// Raw bytes of a C-contiguous buffer of any shape and item type.
std::string_view BytesView(const py::buffer& b, py::buffer_info& info) {
  info = b.request();
  py::ssize_t expected = info.itemsize;
  for (py::ssize_t i = info.ndim - 1; i >= 0; --i) {
    if (info.shape[i] != 1 && info.strides[i] != expected)
      throw std::invalid_argument("buffer must be C-contiguous");
    expected *= info.shape[i];
  }
  return {static_cast<const char*>(info.ptr),
          static_cast<std::size_t>(info.size * info.itemsize)};
}

py::bytes EncodeDataPy(py::array_t<std::complex<float>, py::array::c_style | py::array::forcecast> data,
                       unsigned bits, const std::string& distribution, double truncation,
                       const std::string& normalization, std::uint64_t seed,
                       std::optional<std::vector<std::uint32_t>> antenna1,
                       std::optional<std::vector<std::uint32_t>> antenna2,
                       std::uint32_t n_antennas, bool embed_dictionary,
                       bool embed_antenna_map, unsigned threads) {
  const Shape shape = ToShape(ShapeOf(data));
  const DataParams p = MakeParams(bits, distribution, truncation, normalization, seed,
                                  embed_dictionary, embed_antenna_map);
  const auto map = MakeMap(antenna1, antenna2, n_antennas);
  const AntennaMap* map_ptr = map ? &*map : nullptr;
  const std::size_t size = EncodedSize(p, shape, map_ptr);
  std::string out(size, '\0');
  const std::complex<float>* in = data.data();
  {
    py::gil_scoped_release release;
    EncodeData(p, shape, map_ptr, in, reinterpret_cast<std::byte*>(out.data()), out.size(),
               {threads});
  }
  return py::bytes(out);
}

py::array_t<std::complex<float>> DecodeDataPy(const py::buffer& encoded,
                                               const std::vector<std::size_t>& shape_vec,
                                               std::optional<std::vector<std::uint32_t>> antenna1,
                                               std::optional<std::vector<std::uint32_t>> antenna2,
                                               std::uint32_t n_antennas) {
  const Shape shape = ToShape(shape_vec);
  const auto map = MakeMap(antenna1, antenna2, n_antennas);
  py::buffer_info info;
  const std::string_view in = BytesView(encoded, info);
  py::array_t<std::complex<float>> out(std::vector<py::ssize_t>(shape_vec.begin(), shape_vec.end()));
  std::complex<float>* out_ptr = out.mutable_data();
  {
    py::gil_scoped_release release;
    DecodeData(reinterpret_cast<const std::byte*>(in.data()), in.size(), shape,
               map ? &*map : nullptr, out_ptr);
  }
  return out;
}

py::bytes EncodeWeightsPy(py::array_t<float, py::array::c_style | py::array::forcecast> data,
                          unsigned bits, unsigned threads) {
  const Shape shape = ToShape(ShapeOf(data));
  WeightParams p;
  p.bits = bits;
  std::string out(EncodedSize(p, shape), '\0');
  const float* in = data.data();
  {
    py::gil_scoped_release release;
    EncodeWeights(p, shape, in, reinterpret_cast<std::byte*>(out.data()), out.size(), {threads});
  }
  return py::bytes(out);
}

py::array_t<float> DecodeWeightsPy(const py::buffer& encoded,
                                   const std::vector<std::size_t>& shape_vec) {
  const Shape shape = ToShape(shape_vec);
  py::buffer_info info;
  const std::string_view in = BytesView(encoded, info);
  py::array_t<float> out(std::vector<py::ssize_t>(shape_vec.begin(), shape_vec.end()));
  float* out_ptr = out.mutable_data();
  {
    py::gil_scoped_release release;
    DecodeWeights(reinterpret_cast<const std::byte*>(in.data()), in.size(), shape, out_ptr);
  }
  return out;
}

py::dict ReadHeaderPy(const py::buffer& encoded) {
  py::buffer_info info;
  const std::string_view in = BytesView(encoded, info);
  const ChunkHeader h = ReadHeader(reinterpret_cast<const std::byte*>(in.data()), in.size(),
                                   /*require_complete_chunk=*/false);
  py::dict d;
  d["format_version"] = h.format_version;
  d["header_bytes"] = h.header_bytes;
  d["kind"] = ToString(h.kind);
  d["bits"] = h.bits;
  if (h.kind == Kind::kData) {
    d["distribution"] = ToString(h.GetDistribution());
    d["normalization"] = ToString(h.GetNormalization());
    d["truncation"] = h.truncation;
    d["n_antennas"] = h.n_antenna;
    d["dictionary_embedded"] = (h.flags & kFlagDictionaryEmbedded) != 0;
    d["antenna_map_embedded"] = (h.flags & kFlagAntennaMapEmbedded) != 0;
  }
  d["flags"] = h.flags;
  d["shape"] = py::make_tuple(h.n_time, h.n_baseline, h.n_channel, h.n_pol);
  d["nonce"] = h.nonce;
  d["dict_checksum"] = h.dict_checksum;
  d["side_floats_per_block"] = h.side_floats_per_block;
  d["symbol_bytes_per_block"] = h.symbol_bytes_per_block;
  d["encoded_size"] = EncodedSize(h);
  return d;
}

}  // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "dp3core.dysco core binding (unofficial DP3Core convention)";
  py::register_exception<FormatError>(m, "FormatError", PyExc_ValueError);

  m.attr("__version__") = LibraryVersion();
  m.attr("FORMAT_VERSION") = kFormatVersion;

  m.def("encode_data", &EncodeDataPy, py::arg("data"), py::kw_only(), py::arg("bits") = 10,
        py::arg("distribution") = "truncated_gaussian", py::arg("truncation") = 2.5,
        py::arg("normalization") = "af", py::arg("seed") = 0, py::arg("antenna1") = py::none(),
        py::arg("antenna2") = py::none(), py::arg("n_antennas") = 0,
        py::arg("embed_dictionary") = false, py::arg("embed_antenna_map") = false,
        py::arg("threads") = 1,
        "Encode a C-order (time, baseline, frequency, polarization) complex64 array.");
  m.def("decode_data", &DecodeDataPy, py::arg("encoded"), py::arg("shape"), py::kw_only(),
        py::arg("antenna1") = py::none(), py::arg("antenna2") = py::none(),
        py::arg("n_antennas") = 0, "Decode a dp3core.dysco chunk into complex64.");
  m.def("encode_weights", &EncodeWeightsPy, py::arg("data"), py::kw_only(),
        py::arg("bits") = 12, py::arg("threads") = 1,
        "Encode a (time, baseline, frequency, polarization) float32 weights array.");
  m.def("decode_weights", &DecodeWeightsPy, py::arg("encoded"), py::arg("shape"),
        "Decode a dp3core.dysco-weights chunk into float32.");
  m.def("read_header", &ReadHeaderPy, py::arg("encoded"), "Parse a chunk header.");
  m.def(
      "encoded_size_data",
      [](const std::vector<std::size_t>& shape, unsigned bits, const std::string& distribution,
         double truncation, const std::string& normalization,
         std::optional<std::vector<std::uint32_t>> antenna1,
         std::optional<std::vector<std::uint32_t>> antenna2, std::uint32_t n_antennas,
         bool embed_dictionary, bool embed_antenna_map) {
        const DataParams p = MakeParams(bits, distribution, truncation, normalization, 0,
                                        embed_dictionary, embed_antenna_map);
        const auto map = MakeMap(antenna1, antenna2, n_antennas);
        return EncodedSize(p, ToShape(shape), map ? &*map : nullptr);
      },
      py::arg("shape"), py::kw_only(), py::arg("bits") = 10,
      py::arg("distribution") = "truncated_gaussian", py::arg("truncation") = 2.5,
      py::arg("normalization") = "af", py::arg("antenna1") = py::none(),
      py::arg("antenna2") = py::none(), py::arg("n_antennas") = 0,
      py::arg("embed_dictionary") = false, py::arg("embed_antenna_map") = false);
  m.def(
      "encoded_size_weights",
      [](const std::vector<std::size_t>& shape, unsigned bits) {
        WeightParams p;
        p.bits = bits;
        return EncodedSize(p, ToShape(shape));
      },
      py::arg("shape"), py::kw_only(), py::arg("bits") = 12);
  m.def(
      "decode_table",
      [](unsigned bits, const std::string& distribution, double truncation) {
        const auto t = GetCachedTable(bits, ParseDistribution(distribution), truncation);
        py::array_t<float> out(static_cast<py::ssize_t>(t->levels.size()));
        std::memcpy(out.mutable_data(), t->levels.data(), t->levels.size() * 4);
        return out;
      },
      py::arg("bits"), py::arg("distribution") = "truncated_gaussian", py::arg("truncation") = 2.5);
  m.def(
      "xxh3",
      [](const py::buffer& b) {
        py::buffer_info info;
        const std::string_view v = BytesView(b, info);
        return Xxh3(v.data(), v.size());
      },
      py::arg("data"), "XXH3-64 (seed 0) of a buffer's bytes.");
}
