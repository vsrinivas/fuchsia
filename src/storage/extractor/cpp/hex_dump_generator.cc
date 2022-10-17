// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hex_dump_generator.h"

#include <lib/cksum.h>
#include <lib/zx/status.h>
#include <sys/stat.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>
#include <iomanip>
#include <memory>
#include <string>

#include <fbl/unique_fd.h>

namespace extractor {

zx::result<std::unique_ptr<HexDumpGenerator>> HexDumpGenerator::Create(
    fbl::unique_fd input, HexDumpGeneratorOptions options) {
  if (!input || options.bytes_per_line == 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (lseek(input.get(), 0, SEEK_SET) != 0) {
    return zx::error(ZX_ERR_IO);
  }

  struct stat st;
  if (fstat(input.get(), &st) != 0) {
    return zx::error(ZX_ERR_FILE_BIG);
  }

  return zx::ok(std::unique_ptr<HexDumpGenerator>(
      new HexDumpGenerator(std::move(input), st.st_size, std::move(options))));
}

std::string HexDumpGenerator::BuildLine(const std::string& hex_string, off_t offset,
                                        size_t size) const {
  std::stringstream stream;

  if (options_.tag.length() > 0) {
    stream << options_.tag << " ";
  }
  if (options_.dump_offset) {
    stream << offset << "-" << offset + size - 1 << ":";
  }

  stream << hex_string << std::endl;
  return stream.str();
}

zx::result<std::string> HexDumpGenerator::DumpToString(const uint8_t* buffer, off_t offset,
                                                       size_t size) {
  ZX_ASSERT(size > 0 && size <= options_.bytes_per_line);
  std::stringstream stream;

  std::stringstream current_line;
  for (size_t i = 0; i < size; i++) {
    current_line << std::setfill('0') << std::setw(2) << std::right << std::hex << +buffer[i];
  }

  if (last_hex_string_ != current_line.str()) {
    if (skipped_bytes_ > 0) {
      stream << BuildLine("*", skip_start_offset_, skipped_bytes_);
    }

    stream << BuildLine(current_line.str(), offset, size);
    last_hex_string_ = current_line.str();
    skipped_bytes_ = 0;
  } else {
    if (skipped_bytes_ == 0) {
      skip_start_offset_ = offset;
    }
    skipped_bytes_ += options_.bytes_per_line;
  }

  if (Done()) {
    // If we are done dumping the file then the last line might be a repetation of previous line. If
    // so dump the repeated line.
    if (skipped_bytes_ > 0) {
      stream << BuildLine("*", skip_start_offset_, skipped_bytes_);
    }

    if (options_.dump_checksum) {
      stream << BuildLine("checksum: " + std::to_string(crc32_), 0, file_size_);
    }
  }

  return zx::ok(stream.str());
}

bool HexDumpGenerator::Done() const { return current_offset_ == file_size_; }

zx::result<std::string> HexDumpGenerator::GetNextLine() {
  if (Done()) {
    return zx::error(ZX_ERR_STOP);
  }

  size_t bytes_read = 0;

  uint8_t buffer[options_.bytes_per_line];

  while (bytes_read != options_.bytes_per_line) {
    auto ret = read(input_.get(), buffer + bytes_read, options_.bytes_per_line - bytes_read);

    if (ret < 0) {
      return zx::error(ZX_ERR_IO);
    }

    if (ret == 0) {
      break;
    }

    bytes_read += static_cast<size_t>(ret);
  }

  if (bytes_read == 0) {
    // We have checked whether we are "Done()" in the beginning. read should not return zero.
    return zx::error(ZX_ERR_BAD_STATE);
  }

  if (options_.dump_checksum) {
    crc32_ = crc32(crc32_, buffer, bytes_read);
  }

  off_t offset = current_offset_;
  current_offset_ += static_cast<off_t>(bytes_read);
  auto status = DumpToString(buffer, offset, bytes_read);
  return status;
}

}  // namespace extractor
