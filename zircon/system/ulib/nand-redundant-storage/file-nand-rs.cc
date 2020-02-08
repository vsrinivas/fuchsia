// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/nand-redundant-storage/file-nand-redundant-storage.h"

#include <fcntl.h>
#include <lib/cksum.h>
#include <lib/log/log.h>
#include <lib/nand-redundant-storage/nand-redundant-storage-header.h>
#include <lib/nand-redundant-storage/nand-redundant-storage-interface.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

namespace nand_rs {

namespace {

constexpr uint8_t kNandBlankByte = 0xFF;

}  // namespace

FileNandRedundantStorage::FileNandRedundantStorage(fbl::unique_fd file, uint32_t block_size,
                                                   uint32_t page_size)
    : file_(std::move(file)), block_size_(block_size), page_size_(page_size) {}

uint32_t FileNandRedundantStorage::BlockSize() const { return block_size_; }

uint32_t FileNandRedundantStorage::PageSize() const { return page_size_; }

zx_status_t FileNandRedundantStorage::WriteBuffer(const std::vector<uint8_t>& buffer,
                                                  uint32_t num_copies, uint32_t* num_copies_written,
                                                  bool skip_recovery_header) {
  ZX_DEBUG_ASSERT(file_);
  ZX_ASSERT(num_copies_written);
  ZX_ASSERT(num_copies != 0);
  ZX_ASSERT(!buffer.empty());

  const uint32_t header_offset = (skip_recovery_header ? 0 : kNandRsHeaderSize);

  ZX_ASSERT_MSG(buffer.size() <= BlockSize() - header_offset, "File size too large");

  *num_copies_written = 0;

  // Repeated calls to WriteBuffer overwrites previous data.
  // Seek to the front of the file then truncate the file to 0 bytes.
  lseek(file_.get(), 0, SEEK_SET);
  ftruncate(file_.get(), 0);

  std::vector<uint8_t> block_buffer(block_size_, kNandBlankByte);

  // If requested, write header into the front of the block sized buffer.
  if (!skip_recovery_header) {
    NandRsHeader header = MakeHeader(buffer);
    memcpy(block_buffer.data(), &header, kNandRsHeaderSize);
  }

  // Write contents into block sized buffer.
  memcpy(block_buffer.data() + header_offset, buffer.data(), buffer.size());

  // Pad last page with zeros.
  uint32_t page_overflow = (buffer.size() + header_offset) % page_size_;
  uint32_t page_remaining = page_size_ - page_overflow;
  memset(block_buffer.data() + header_offset + buffer.size(), 0, page_remaining);

  // Write block sized buffer to file |num_copies| times.
  for (uint32_t i = 0; i < num_copies; ++i) {
    write(file_.get(), block_buffer.data(), block_size_);
    (*num_copies_written)++;
  }

  return ZX_OK;
}

zx_status_t FileNandRedundantStorage::ReadToBuffer(std::vector<uint8_t>* out_buffer,
                                                   bool skip_recovery_header, size_t file_size) {
  ZX_DEBUG_ASSERT(file_);
  ZX_ASSERT(out_buffer);

  if (skip_recovery_header && file_size <= 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::vector<uint8_t> block_buffer(block_size_, 0);

  int64_t real_file_size = lseek(file_.get(), 0, SEEK_END);
  for (uint32_t offset = 0; offset < real_file_size; offset += block_size_) {
    lseek(file_.get(), offset, SEEK_SET);
    read(file_.get(), block_buffer.data(), block_size_);

    uint32_t copy_offset = 0;

    if (!skip_recovery_header) {
      copy_offset = kNandRsHeaderSize;
      auto header = ReadHeader(block_buffer, block_size_);

      if (!header) {
        fprintf(stderr, "Error validating data at offset %d\n", offset);
        continue;
      }

      file_size = header->file_size;
    }

    out_buffer->resize(file_size, 0);
    memcpy(out_buffer->data(), block_buffer.data() + copy_offset, file_size);
    return ZX_OK;
  }

  fprintf(stderr, "No valid files found.\n");
  return ZX_ERR_IO;
}

}  // namespace nand_rs
