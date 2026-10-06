// Read-only parser for a casacore DyscoStMan data file.
// SPDX-License-Identifier: GPL-3.0-only
//
// The on-disk layout follows aroffringa/dysco header.h and
// threadeddyscocolumn.h (file format 1.0): a serialized header, then one
// generic column header plus an 8-byte extra header per column. The stored
// headerSize is larger than those bytes, because Dysco adds sizeof of a
// header type that carries a vtable pointer. Blocks start at headerSize.
// Each column block is native-endian float32 side data followed by packed
// symbols. This parser does not decode symbols.
#ifndef DP3CORE_DYSCO_HARNESS_DYSCO_FILE_H_
#define DP3CORE_DYSCO_HARNESS_DYSCO_FILE_H_

#include <cstdint>
#include <string>
#include <vector>

namespace dp3core::dysco::harness {

/// One column inside a DyscoStMan file. `offset` is its start within a block.
struct DyscoColumn {
  std::uint32_t block_bytes = 0;
  std::uint32_t offset = 0;
  std::uint32_t antenna_count = 0;
};

/// Fields of the DyscoStMan file header. `distribution_raw` is Dysco's own
/// enum (0 Gaussian, 1 Uniform, 2 Student's T, 3 TruncatedGaussian), which is
/// not the codec header's distribution code.
struct DyscoHeader {
  std::uint32_t header_size = 0;
  std::uint32_t block_size = 0;
  std::uint32_t rows_per_block = 0;
  std::uint32_t antenna_count = 0;
  std::uint32_t n_blocks = 0;
  std::uint8_t data_bit_count = 0;
  std::uint8_t weight_bit_count = 0;
  std::uint8_t distribution_raw = 0;
  std::uint8_t normalization = 0;
  double truncation = 0.0;
  double student_t_nu = 0.0;
  std::string storage_manager_name;
  std::vector<DyscoColumn> columns;
};

class DyscoFile {
 public:
  DyscoFile() = default;
  /// Opens and parses @p path. Throws std::runtime_error on a short or
  /// inconsistent file. The file is not loaded into memory.
  explicit DyscoFile(const std::string& path);

  const DyscoHeader& header() const { return header_; }
  const std::string& path() const { return path_; }

  /// Copies column @p column of block @p block into @p out, which must hold
  /// header().columns[column].block_bytes bytes.
  void ReadColumnBlock(std::uint32_t block, std::uint32_t column, void* out) const;

 private:
  std::string path_;
  DyscoHeader header_;
};

}  // namespace dp3core::dysco::harness

#endif
