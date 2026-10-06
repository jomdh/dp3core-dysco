// SPDX-License-Identifier: GPL-3.0-only
#include "dysco_file.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace dp3core::dysco::harness {
namespace {

class Reader {
 public:
  explicit Reader(const std::string& path) : in_(path, std::ios::binary) {
    if (!in_) throw std::runtime_error("cannot open Dysco file " + path);
  }

  template <typename T>
  T Read() {
    T value{};
    in_.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in_) throw std::runtime_error("truncated Dysco header");
    return value;
  }

  std::string ReadString32() {
    const auto n = Read<std::uint32_t>();
    if (n > 1024) throw std::runtime_error("Dysco storage-manager name is longer than 1024 bytes");
    std::string s(n, '\0');
    if (n != 0) in_.read(s.data(), static_cast<std::streamsize>(n));
    if (!in_) throw std::runtime_error("truncated Dysco storage-manager name");
    return s;
  }

  std::uint64_t Position() {
    const auto p = in_.tellg();
    if (p < 0) throw std::runtime_error("cannot tell Dysco file position");
    return static_cast<std::uint64_t>(p);
  }

  std::uint64_t Size() {
    in_.seekg(0, std::ios::end);
    const auto n = in_.tellg();
    if (n < 0) throw std::runtime_error("cannot tell Dysco file size");
    return static_cast<std::uint64_t>(n);
  }

  void Seek(std::uint64_t pos) {
    in_.seekg(static_cast<std::streamoff>(pos));
    if (!in_) throw std::runtime_error("cannot seek in Dysco file");
  }

  void ReadBytes(void* out, std::uint64_t n) {
    in_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n));
    if (!in_) throw std::runtime_error("short read in Dysco file");
  }

 private:
  std::ifstream in_;
};

}  // namespace

DyscoFile::DyscoFile(const std::string& path) : path_(path) {
  Reader r(path);
  header_.header_size = r.Read<std::uint32_t>();
  const auto column_header_offset = r.Read<std::uint32_t>();
  const auto column_count = r.Read<std::uint32_t>();
  header_.storage_manager_name = r.ReadString32();
  header_.rows_per_block = r.Read<std::uint32_t>();
  header_.antenna_count = r.Read<std::uint32_t>();
  header_.block_size = r.Read<std::uint32_t>();
  const auto version_major = r.Read<std::uint16_t>();
  const auto version_minor = r.Read<std::uint16_t>();
  header_.data_bit_count = r.Read<std::uint8_t>();
  header_.weight_bit_count = r.Read<std::uint8_t>();
  header_.distribution_raw = r.Read<std::uint8_t>();
  header_.normalization = r.Read<std::uint8_t>();
  header_.student_t_nu = r.Read<double>();
  header_.truncation = r.Read<double>();
  if (version_major != 1 || version_minor != 0)
    throw std::runtime_error("Dysco file " + path + " is not format 1.0");
  if (r.Position() != column_header_offset)
    throw std::runtime_error("Dysco column-header offset does not match the parsed header in " + path);
  if (column_count == 0 || column_count > 8)
    throw std::runtime_error("Dysco file " + path + " has an implausible column count");

  header_.columns.resize(column_count);
  std::uint32_t offset = 0;
  for (std::uint32_t i = 0; i != column_count; ++i) {
    const auto column_header_size = r.Read<std::uint32_t>();
    if (column_header_size < 12)
      throw std::runtime_error("Dysco column header is shorter than 12 bytes in " + path);
    DyscoColumn col;
    col.block_bytes = r.Read<std::uint32_t>();
    col.antenna_count = r.Read<std::uint32_t>();
    col.offset = offset;
    const auto consumed = std::uint32_t{12};
    if (column_header_size > consumed) {
      std::vector<char> skip(column_header_size - consumed);
      r.ReadBytes(skip.data(), skip.size());
    }
    offset += col.block_bytes;
    header_.columns[i] = col;
  }
  if (offset != header_.block_size)
    throw std::runtime_error("Dysco column block sizes do not add up to the file block size in " + path);
  // Dysco stores headerSize as columnHeaderOffset plus, per column,
  // sizeof(GenericColumnHeader) + ExtraHeaderSize(). GenericColumnHeader has a
  // vtable pointer, so sizeof is 16 while only 4 bytes are written. Blocks
  // still start at headerSize. The serialized headers must not run past it.
  if (r.Position() > header_.header_size)
    throw std::runtime_error("Dysco parsed headers run past header_size in " + path + " (" +
                            std::to_string(r.Position()) + " > " +
                            std::to_string(header_.header_size) + ")");
  const auto file_size = r.Size();
  if (file_size < header_.header_size)
    throw std::runtime_error("Dysco file " + path + " is shorter than its header");
  const auto payload = file_size - header_.header_size;
  if (header_.block_size == 0 || payload % header_.block_size != 0)
    throw std::runtime_error("Dysco payload is not a whole number of blocks in " + path);
  header_.n_blocks = static_cast<std::uint32_t>(payload / header_.block_size);
}

void DyscoFile::ReadColumnBlock(std::uint32_t block, std::uint32_t column, void* out) const {
  if (block >= header_.n_blocks || column >= header_.columns.size())
    throw std::runtime_error("Dysco block index out of range in " + path_);
  const auto& col = header_.columns[column];
  const std::uint64_t pos = static_cast<std::uint64_t>(header_.header_size) +
                            static_cast<std::uint64_t>(block) * header_.block_size + col.offset;
  std::ifstream in(path_, std::ios::binary);
  if (!in) throw std::runtime_error("cannot reopen Dysco file " + path_);
  in.seekg(static_cast<std::streamoff>(pos));
  in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(col.block_bytes));
  if (!in) throw std::runtime_error("short Dysco block read in " + path_);
}

}  // namespace dp3core::dysco::harness
