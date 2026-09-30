// dp3core_dysco_ts_copy SRC DST INPUT DECODED
//
// Interop helper for python/tests/test_tensorstore_interop.py:
//  * opens the zarr3 array SRC (written by zarr-python) with TensorStore and
//    writes its decoded elements, C order, to the raw file DECODED;
//  * creates DST with the zarr.json metadata of SRC and writes the raw C-order
//    elements from INPUT (the same original data zarr-python encoded).
// The nonce is a hash of the encoder input (design 5.2), so byte-identical
// chunks require both implementations to encode the same original input;
// re-encoding the lossy decoded data would legitimately give other bytes.
// SPDX-License-Identifier: GPL-3.0-only
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "tensorstore/array.h"
#include "tensorstore/contiguous_layout.h"
#include "tensorstore/open.h"
#include "tensorstore/open_mode.h"
#include "tensorstore/tensorstore.h"

namespace {
std::string Slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: " << argv[0] << " SRC DST INPUT DECODED\n";
    return 2;
  }
  const std::string src = argv[1], dst = argv[2], input = argv[3], decoded = argv[4];
  const auto metadata = nlohmann::json::parse(Slurp(src + "/zarr.json"));
  auto source = tensorstore::Open({{"driver", "zarr3"},
                                   {"kvstore", {{"driver", "file"}, {"path", src + "/"}}}},
                                  tensorstore::OpenMode::open, tensorstore::ReadWriteMode::read)
                    .result();
  if (!source.ok()) {
    std::cerr << "open " << src << ": " << source.status() << '\n';
    return 1;
  }
  auto data = tensorstore::Read<tensorstore::zero_origin>(*source).result();
  if (!data.ok()) {
    std::cerr << "read: " << data.status() << '\n';
    return 1;
  }
  const auto dtype = source->dtype();
  auto c_data = tensorstore::MakeCopy(*data, tensorstore::c_order);
  {
    std::ofstream out(decoded, std::ios::binary);
    out.write(static_cast<const char*>(c_data.data()),
              static_cast<std::streamsize>(c_data.num_elements() * dtype.size()));
  }

  auto target = tensorstore::Open({{"driver", "zarr3"},
                                   {"kvstore", {{"driver", "file"}, {"path", dst + "/"}}},
                                   {"metadata", metadata}},
                                  tensorstore::OpenMode::create, tensorstore::ReadWriteMode::read_write)
                    .result();
  if (!target.ok()) {
    std::cerr << "create " << dst << ": " << target.status() << '\n';
    return 1;
  }
  auto original = tensorstore::AllocateArray(c_data.shape(), tensorstore::c_order,
                                             tensorstore::default_init, dtype);
  const std::string raw = Slurp(input);
  const auto bytes = static_cast<std::size_t>(original.num_elements() * dtype.size());
  if (raw.size() != bytes) {
    std::cerr << "input " << input << ": " << raw.size() << " bytes, expected " << bytes << '\n';
    return 1;
  }
  std::memcpy(original.data(), raw.data(), bytes);
  const auto st = tensorstore::Write(original, *target).commit_future.result();
  if (!st.ok()) {
    std::cerr << "write: " << st.status() << '\n';
    return 1;
  }
  return 0;
}
