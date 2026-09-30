// "dp3core.dysco" and "dp3core.dysco-weights" as TensorStore zarr3
// array-to-bytes codecs, registered from this translation unit with
// TensorStore's codec registry (design 7.3, option C). No TensorStore source
// is modified; this file uses TensorStore's internal zarr3 codec API
// (tensorstore/driver/zarr3/codec/*.h, pinned v0.1.85).
// SPDX-License-Identifier: GPL-3.0-only
#include "dp3core/dysco/tensorstore.h"

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "riegeli/bytes/read_all.h"
#include "riegeli/bytes/reader.h"
#include "riegeli/bytes/writer.h"
#include "tensorstore/array.h"
#include "tensorstore/contiguous_layout.h"
#include "tensorstore/data_type.h"
#include "tensorstore/driver/zarr3/codec/codec.h"
#include "tensorstore/driver/zarr3/codec/codec_spec.h"
#include "tensorstore/driver/zarr3/codec/registry.h"
#include "tensorstore/index.h"
#include "tensorstore/index_space/dimension_permutation.h"
#include "tensorstore/internal/global_initializer.h"
#include "tensorstore/internal/intrusive_ptr.h"
#include "tensorstore/internal/json_binding/json_binding.h"
#include "tensorstore/internal/json_binding/std_array.h"
#include "tensorstore/internal/json_binding/std_optional.h"
#include "tensorstore/util/result.h"
#include "tensorstore/util/span.h"
#include "tensorstore/util/status.h"

#include "dp3core/dysco/codec.h"
#include "dp3core/dysco/version.h"

namespace dp3core::dysco::tensorstore {
namespace {

namespace ts = ::tensorstore;
namespace tz = ::tensorstore::internal_zarr3;
namespace jb = ::tensorstore::internal_json_binding;

std::atomic<bool> g_registered{false};

const std::vector<std::string>& CanonicalAxes() {
  static const std::vector<std::string> axes = {"time", "baseline", "frequency",
                                                "polarization"};
  return axes;
}

// Runs a core call, turning its exceptions into absl::Status.
template <typename Fn>
absl::Status Guard(Fn&& fn) {
  try {
    fn();
    return absl::OkStatus();
  } catch (const FormatError& e) {
    return absl::DataLossError(e.what());
  } catch (const std::invalid_argument& e) {
    return absl::InvalidArgumentError(e.what());
  } catch (const std::exception& e) {
    return absl::InternalError(e.what());
  }
}

absl::Status CheckShape(ts::span<const ts::Index> shape, std::string_view name) {
  if (shape.size() != 4)
    return absl::InvalidArgumentError(absl::StrFormat(
        "\"%s\" codec needs rank-4 (time, baseline, frequency, polarization) "
        "chunks, got rank %d",
        name, shape.size()));
  return absl::OkStatus();
}

Shape ToShape(ts::span<const ts::Index> s) {
  return {static_cast<std::size_t>(s[0]), static_cast<std::size_t>(s[1]),
          static_cast<std::size_t>(s[2]), static_cast<std::size_t>(s[3])};
}

// Same decoded-layout rules as TensorStore's "bytes" codec: C order only.
absl::Status CLayout(const tz::ArrayDataTypeAndShapeInfo& array_info,
                     tz::ArrayCodecChunkLayoutInfo& decoded) {
  const ts::DimensionIndex rank = array_info.rank;
  if (rank != ts::dynamic_rank) {
    auto& inner_order = decoded.inner_order.emplace();
    for (ts::DimensionIndex i = 0; i < rank; ++i) inner_order[i] = i;
  }
  if (array_info.shape) {
    auto& read_chunk_shape = decoded.read_chunk_shape.emplace();
    for (ts::DimensionIndex i = 0; i < rank; ++i) read_chunk_shape[i] = (*array_info.shape)[i];
  }
  return absl::OkStatus();
}

bool SupportsCOrder(const tz::ArrayCodecResolveParameters& decoded,
                    ts::span<ts::DimensionIndex> preferred_inner_order) {
  if (!decoded.inner_order) return true;
  if (ts::PermutationMatchesOrder(ts::span(decoded.inner_order->data(), decoded.rank),
                                  ts::c_order))
    return true;
  ts::SetPermutation(ts::c_order, preferred_inner_order);
  return false;
}

absl::Status CheckResolveParameters(const tz::ArrayCodecResolveParameters& decoded,
                                    ts::DataType expected, std::string_view name) {
  if (decoded.dtype != expected)
    return absl::InvalidArgumentError(absl::StrFormat(
        "\"%s\" codec needs data type %v, got %v", name, expected, decoded.dtype));
  if (decoded.rank != 4)
    return absl::InvalidArgumentError(absl::StrFormat(
        "\"%s\" codec needs rank 4 (time, baseline, frequency, polarization), got %d",
        name, decoded.rank));
  if (decoded.codec_chunk_shape)
    return absl::InvalidArgumentError(
        absl::StrFormat("\"%s\" codec does not support codec_chunk_shape", name));
  if (decoded.inner_order) {
    for (ts::DimensionIndex i = 0; i < 4; ++i)
      if ((*decoded.inner_order)[i] != i)
        return absl::InvalidArgumentError(
            absl::StrFormat("\"%s\" codec supports only C order", name));
  }
  return absl::OkStatus();
}

// Copies `decoded` into a C-order array unless it already is one.
ts::SharedArrayView<const void> AsCOrder(ts::SharedArrayView<const void> decoded) {
  if (ts::IsContiguousLayout(decoded, ts::c_order)) return decoded;
  auto copy = ts::AllocateArray(decoded.shape(), ts::c_order, ts::default_init, decoded.dtype());
  ts::CopyArray(decoded, copy);
  return copy;
}

// seed: a JSON unsigned integer or a decimal string (design 4.1).
constexpr auto SeedBinder() {
  return [](auto is_loading, const auto& options, auto* obj,
            ::nlohmann::json* j) -> absl::Status {
    if constexpr (is_loading) {
      if (const auto* s = j->template get_ptr<const std::string*>()) {
        if (!absl::SimpleAtoi(*s, obj))
          return absl::InvalidArgumentError(
              absl::StrFormat("seed %s is not a decimal uint64", j->dump()));
        return absl::OkStatus();
      }
      return jb::Integer<std::uint64_t>()(is_loading, options, obj, j);
    } else {
      *j = *obj;
      return absl::OkStatus();
    }
  };
}

// ---------------------------------------------------------------------------
// dp3core.dysco

class DyscoCodecSpec : public tz::ZarrArrayToBytesCodecSpec {
 public:
  struct Options {
    std::optional<int> format_version;
    std::optional<int> bits;
    std::optional<std::string> distribution;
    std::optional<double> truncation;
    std::optional<std::string> normalization;
    std::optional<std::vector<std::string>> axes;
    std::optional<std::uint32_t> n_antennas;
    std::optional<std::vector<std::uint32_t>> antenna1;
    std::optional<std::vector<std::uint32_t>> antenna2;
    std::optional<std::string> prng;
    std::optional<std::uint64_t> seed;
    std::optional<std::string> dictionary;
    std::optional<bool> embed_antenna_map;
  };

  DyscoCodecSpec() = default;
  explicit DyscoCodecSpec(Options o) : options(std::move(o)) {}

  absl::Status MergeFrom(const tz::ZarrCodecSpec& other, bool strict) override;
  tz::ZarrCodecSpec::Ptr Clone() const override {
    return ts::internal::MakeIntrusivePtr<DyscoCodecSpec>(*this);
  }
  absl::Status GetDecodedChunkLayout(const tz::ArrayDataTypeAndShapeInfo& array_info,
                                     tz::ArrayCodecChunkLayoutInfo& decoded) const override {
    return CLayout(array_info, decoded);
  }
  bool SupportsInnerOrder(const tz::ArrayCodecResolveParameters& decoded,
                          ts::span<ts::DimensionIndex> preferred_inner_order) const override {
    return SupportsCOrder(decoded, preferred_inner_order);
  }
  ts::Result<tz::ZarrArrayToBytesCodec::Ptr> Resolve(
      tz::ArrayCodecResolveParameters&& decoded, tz::BytesCodecResolveParameters& encoded,
      tz::ZarrArrayToBytesCodecSpec::Ptr* resolved_spec) const override;

  Options options;
};

class DyscoCodec : public tz::ZarrArrayToBytesCodec {
 public:
  DataParams params;
  std::optional<AntennaMap> map;

  class State : public tz::ZarrArrayToBytesCodec::PreparedState {
   public:
    int64_t encoded_size() const final { return encoded_size_; }
    absl::Status EncodeArray(ts::SharedArrayView<const void> decoded,
                             riegeli::Writer& writer) const final {
      const auto array = AsCOrder(std::move(decoded));
      std::string out(static_cast<std::size_t>(encoded_size_), '\0');
      TENSORSTORE_RETURN_IF_ERROR(Guard([&] {
        EncodeData(codec_->params, shape_, map(),
                   static_cast<const std::complex<float>*>(array.data()),
                   reinterpret_cast<std::byte*>(out.data()), out.size());
      }));
      if (!writer.Write(out)) return writer.status();
      return absl::OkStatus();
    }
    ts::Result<ts::SharedArray<const void>> DecodeArray(ts::span<const ts::Index> decoded_shape,
                                                        riegeli::Reader& reader) const final {
      absl::string_view in;
      TENSORSTORE_RETURN_IF_ERROR(riegeli::ReadAll(reader, in));
      auto out = ts::AllocateArray(decoded_shape, ts::c_order, ts::default_init,
                                   ts::dtype_v<ts::dtypes::complex64_t>);
      TENSORSTORE_RETURN_IF_ERROR(Guard([&] {
        DecodeData(reinterpret_cast<const std::byte*>(in.data()), in.size(), shape_, map(),
                   static_cast<std::complex<float>*>(out.data()));
      }));
      return out;
    }
    const AntennaMap* map() const { return codec_->map ? &*codec_->map : nullptr; }

    ts::internal::IntrusivePtr<const DyscoCodec> codec_;
    Shape shape_;
    int64_t encoded_size_ = 0;
  };

  ts::Result<PreparedState::Ptr> Prepare(ts::span<const ts::Index> decoded_shape) const final {
    TENSORSTORE_RETURN_IF_ERROR(CheckShape(decoded_shape, "dp3core.dysco"));
    auto state = ts::internal::MakeIntrusivePtr<State>();
    state->codec_.reset(this);
    state->shape_ = ToShape(decoded_shape);
    TENSORSTORE_RETURN_IF_ERROR(Guard([&] {
      state->encoded_size_ = static_cast<int64_t>(EncodedSize(params, state->shape_, state->map()));
    }));
    return state;
  }
};

absl::Status DyscoCodecSpec::MergeFrom(const tz::ZarrCodecSpec& other, bool strict) {
  using O = Options;
  const auto& b = static_cast<const DyscoCodecSpec&>(other).options;
  auto& a = options;
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::format_version>("format_version", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::bits>("bits", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::distribution>("distribution", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::truncation>("truncation", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::normalization>("normalization", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::axes>("axes", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::n_antennas>("n_antennas", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::antenna1>("antenna1", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::antenna2>("antenna2", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::prng>("prng", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::seed>("seed", a, b, SeedBinder()));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::dictionary>("dictionary", a, b));
  TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::embed_antenna_map>("embed_antenna_map", a, b));
  return absl::OkStatus();
}

ts::Result<tz::ZarrArrayToBytesCodec::Ptr> DyscoCodecSpec::Resolve(
    tz::ArrayCodecResolveParameters&& decoded, tz::BytesCodecResolveParameters& encoded,
    tz::ZarrArrayToBytesCodecSpec::Ptr* resolved_spec) const {
  constexpr std::string_view kName = "dp3core.dysco";
  TENSORSTORE_RETURN_IF_ERROR(
      CheckResolveParameters(decoded, ts::dtype_v<ts::dtypes::complex64_t>, kName));
  Options r = options;  // resolved: every field set
  if (!r.format_version) r.format_version = 1;
  if (*r.format_version < 1 || *r.format_version > static_cast<int>(kFormatVersion))
    return absl::InvalidArgumentError(
        absl::StrFormat("format_version %d is not supported", *r.format_version));
  if (!r.bits) r.bits = 10;
  if (!r.distribution) r.distribution = "truncated_gaussian";
  if (*r.distribution == "truncated_gaussian") {
    if (!r.truncation) r.truncation = 2.5;
  } else {
    r.truncation.reset();
  }
  if (!r.normalization) r.normalization = "af";
  if (!r.axes) r.axes = CanonicalAxes();
  if (*r.axes != CanonicalAxes())
    return absl::InvalidArgumentError(absl::StrFormat(
        "axes [%s]: only [time, baseline, frequency, polarization] is supported",
        absl::StrJoin(*r.axes, ", ")));
  if (!r.prng) r.prng = "splitmix64-v1";
  if (*r.prng != "splitmix64-v1")
    return absl::InvalidArgumentError(absl::StrFormat("prng \"%s\" is not supported", *r.prng));
  if (!r.seed) r.seed = 0;
  if (!r.dictionary) r.dictionary = "computed";
  if (*r.dictionary != "computed" && *r.dictionary != "embedded")
    return absl::InvalidArgumentError(
        absl::StrFormat("dictionary must be \"computed\" or \"embedded\", got \"%s\"", *r.dictionary));
  if (!r.embed_antenna_map) r.embed_antenna_map = false;

  auto codec = ts::internal::MakeIntrusivePtr<DyscoCodec>();
  DataParams& p = codec->params;
  p.bits = static_cast<unsigned>(*r.bits);
  if (*r.distribution == "gaussian") p.distribution = Distribution::kGaussian;
  else if (*r.distribution == "truncated_gaussian") p.distribution = Distribution::kTruncatedGaussian;
  else if (*r.distribution == "uniform") p.distribution = Distribution::kUniform;
  else
    return absl::InvalidArgumentError(absl::StrFormat("unknown distribution \"%s\"", *r.distribution));
  p.truncation = r.truncation.value_or(0.0);
  if (*r.normalization == "af") p.normalization = Normalization::kAF;
  else if (*r.normalization == "rf") p.normalization = Normalization::kRF;
  else if (*r.normalization == "row") p.normalization = Normalization::kRow;
  else
    return absl::InvalidArgumentError(absl::StrFormat("unknown normalization \"%s\"", *r.normalization));
  p.seed = *r.seed;
  p.embed_dictionary = *r.dictionary == "embedded";
  p.embed_antenna_map = *r.embed_antenna_map;
  if (p.normalization == Normalization::kAF) {
    if (!r.n_antennas || !r.antenna1 || !r.antenna2)
      return absl::InvalidArgumentError(
          "normalization \"af\" requires n_antennas, antenna1 and antenna2");
    AntennaMap m;
    m.n_antenna = *r.n_antennas;
    m.antenna1 = *r.antenna1;
    m.antenna2 = *r.antenna2;
    codec->map = std::move(m);
  } else if (r.n_antennas || r.antenna1 || r.antenna2) {
    return absl::InvalidArgumentError(
        "n_antennas/antenna1/antenna2 are only allowed with normalization \"af\"");
  }
  // Parameter checks that don't need the chunk shape (bits, truncation,
  // flags, antenna indices): validate against a one-element-per-axis shape
  // with the map's baseline count.
  const std::size_t n_bl = codec->map ? codec->map->antenna1.size() : 1;
  TENSORSTORE_RETURN_IF_ERROR(Guard([&] {
    Validate(p, Shape{1, n_bl == 0 ? 1 : n_bl, 1, 1}, codec->map ? &*codec->map : nullptr);
  }));
  if (decoded.read_chunk_shape && codec->map &&
      (*decoded.read_chunk_shape)[1] != static_cast<ts::Index>(codec->map->antenna1.size()))
    return absl::InvalidArgumentError(absl::StrFormat(
        "AF: the chunk has %d baselines but antenna1/antenna2 have %d entries",
        (*decoded.read_chunk_shape)[1], codec->map->antenna1.size()));

  encoded.item_bits = 8;
  if (resolved_spec) resolved_spec->reset(new DyscoCodecSpec(std::move(r)));
  return codec;
}

// ---------------------------------------------------------------------------
// dp3core.dysco-weights

class DyscoWeightsCodecSpec : public tz::ZarrArrayToBytesCodecSpec {
 public:
  struct Options {
    std::optional<int> format_version;
    std::optional<int> bits;
    std::optional<std::vector<std::string>> axes;
  };
  DyscoWeightsCodecSpec() = default;
  explicit DyscoWeightsCodecSpec(Options o) : options(std::move(o)) {}

  absl::Status MergeFrom(const tz::ZarrCodecSpec& other, bool strict) override {
    using O = Options;
    const auto& b = static_cast<const DyscoWeightsCodecSpec&>(other).options;
    TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::format_version>("format_version", options, b));
    TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::bits>("bits", options, b));
    TENSORSTORE_RETURN_IF_ERROR(tz::MergeConstraint<&O::axes>("axes", options, b));
    return absl::OkStatus();
  }
  tz::ZarrCodecSpec::Ptr Clone() const override {
    return ts::internal::MakeIntrusivePtr<DyscoWeightsCodecSpec>(*this);
  }
  absl::Status GetDecodedChunkLayout(const tz::ArrayDataTypeAndShapeInfo& array_info,
                                     tz::ArrayCodecChunkLayoutInfo& decoded) const override {
    return CLayout(array_info, decoded);
  }
  bool SupportsInnerOrder(const tz::ArrayCodecResolveParameters& decoded,
                          ts::span<ts::DimensionIndex> preferred_inner_order) const override {
    return SupportsCOrder(decoded, preferred_inner_order);
  }
  ts::Result<tz::ZarrArrayToBytesCodec::Ptr> Resolve(
      tz::ArrayCodecResolveParameters&& decoded, tz::BytesCodecResolveParameters& encoded,
      tz::ZarrArrayToBytesCodecSpec::Ptr* resolved_spec) const override;

  Options options;
};

class DyscoWeightsCodec : public tz::ZarrArrayToBytesCodec {
 public:
  WeightParams params;

  class State : public tz::ZarrArrayToBytesCodec::PreparedState {
   public:
    int64_t encoded_size() const final { return encoded_size_; }
    absl::Status EncodeArray(ts::SharedArrayView<const void> decoded,
                             riegeli::Writer& writer) const final {
      const auto array = AsCOrder(std::move(decoded));
      std::string out(static_cast<std::size_t>(encoded_size_), '\0');
      TENSORSTORE_RETURN_IF_ERROR(Guard([&] {
        EncodeWeights(params_, shape_, static_cast<const float*>(array.data()),
                      reinterpret_cast<std::byte*>(out.data()), out.size());
      }));
      if (!writer.Write(out)) return writer.status();
      return absl::OkStatus();
    }
    ts::Result<ts::SharedArray<const void>> DecodeArray(ts::span<const ts::Index> decoded_shape,
                                                        riegeli::Reader& reader) const final {
      absl::string_view in;
      TENSORSTORE_RETURN_IF_ERROR(riegeli::ReadAll(reader, in));
      auto out = ts::AllocateArray(decoded_shape, ts::c_order, ts::default_init,
                                   ts::dtype_v<ts::dtypes::float32_t>);
      TENSORSTORE_RETURN_IF_ERROR(Guard([&] {
        DecodeWeights(reinterpret_cast<const std::byte*>(in.data()), in.size(), shape_,
                      static_cast<float*>(out.data()));
      }));
      return out;
    }
    WeightParams params_;
    Shape shape_;
    int64_t encoded_size_ = 0;
  };

  ts::Result<PreparedState::Ptr> Prepare(ts::span<const ts::Index> decoded_shape) const final {
    TENSORSTORE_RETURN_IF_ERROR(CheckShape(decoded_shape, "dp3core.dysco-weights"));
    auto state = ts::internal::MakeIntrusivePtr<State>();
    state->params_ = params;
    state->shape_ = ToShape(decoded_shape);
    TENSORSTORE_RETURN_IF_ERROR(Guard([&] {
      state->encoded_size_ = static_cast<int64_t>(EncodedSize(params, state->shape_));
    }));
    return state;
  }
};

ts::Result<tz::ZarrArrayToBytesCodec::Ptr> DyscoWeightsCodecSpec::Resolve(
    tz::ArrayCodecResolveParameters&& decoded, tz::BytesCodecResolveParameters& encoded,
    tz::ZarrArrayToBytesCodecSpec::Ptr* resolved_spec) const {
  TENSORSTORE_RETURN_IF_ERROR(
      CheckResolveParameters(decoded, ts::dtype_v<ts::dtypes::float32_t>, "dp3core.dysco-weights"));
  Options r = options;
  if (!r.format_version) r.format_version = 1;
  if (*r.format_version < 1 || *r.format_version > static_cast<int>(kFormatVersion))
    return absl::InvalidArgumentError(
        absl::StrFormat("format_version %d is not supported", *r.format_version));
  if (!r.bits) r.bits = 12;
  if (!r.axes) r.axes = CanonicalAxes();
  if (*r.axes != CanonicalAxes())
    return absl::InvalidArgumentError("axes: only [time, baseline, frequency, polarization] is supported");
  auto codec = ts::internal::MakeIntrusivePtr<DyscoWeightsCodec>();
  codec->params.bits = static_cast<unsigned>(*r.bits);
  TENSORSTORE_RETURN_IF_ERROR(Guard([&] { Validate(codec->params, Shape{1, 1, 1, 1}); }));
  encoded.item_bits = 8;
  if (resolved_spec) resolved_spec->reset(new DyscoWeightsCodecSpec(std::move(r)));
  return codec;
}

// ---------------------------------------------------------------------------
// Registration

template <auto Member, typename Binder = decltype(jb::DefaultBinder<>)>
constexpr auto Field(const char* name, Binder binder = jb::DefaultBinder<>) {
  return jb::Member(name, jb::Projection<Member>(jb::Optional(binder)));
}

TENSORSTORE_GLOBAL_INITIALIZER {
  {
    using Self = DyscoCodecSpec;
    using O = Self::Options;
    tz::RegisterCodec<Self>(
        "dp3core.dysco",
        jb::Projection<&Self::options>(jb::Sequence(
            Field<&O::format_version>("format_version", jb::Integer<int>(1)),
            Field<&O::bits>("bits", jb::Integer<int>(2, 16)),
            Field<&O::distribution>("distribution"),
            Field<&O::truncation>("truncation"),
            Field<&O::normalization>("normalization"),
            Field<&O::axes>("axes"),
            Field<&O::n_antennas>("n_antennas", jb::Integer<std::uint32_t>(1)),
            Field<&O::antenna1>("antenna1", jb::Array(jb::Integer<std::uint32_t>())),
            Field<&O::antenna2>("antenna2", jb::Array(jb::Integer<std::uint32_t>())),
            Field<&O::prng>("prng"),
            Field<&O::seed>("seed", SeedBinder()),
            Field<&O::dictionary>("dictionary"),
            Field<&O::embed_antenna_map>("embed_antenna_map"))));
  }
  {
    using Self = DyscoWeightsCodecSpec;
    using O = Self::Options;
    tz::RegisterCodec<Self>(
        "dp3core.dysco-weights",
        jb::Projection<&Self::options>(jb::Sequence(
            Field<&O::format_version>("format_version", jb::Integer<int>(1)),
            Field<&O::bits>("bits", jb::Integer<int>(2, 16)),
            Field<&O::axes>("axes"))));
  }
  g_registered = true;
}

}  // namespace

bool CodecsRegistered() { return g_registered.load(); }

}  // namespace dp3core::dysco::tensorstore
