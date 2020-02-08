// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/cksum.h>
#include <lib/mtd/nand-interface.h>
#include <lib/nand-redundant-storage/nand-redundant-storage-header.h>
#include <lib/nand-redundant-storage/nand-redundant-storage-interface.h>
#include <lib/nand-redundant-storage/nand-redundant-storage.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

namespace nand_rs {

namespace {

// Reads the entire block into block_buffer starting at mtd_offset.
int ReadWholeBlock(const std::unique_ptr<mtd::NandInterface>& nand,
                   std::vector<uint8_t>* block_buffer, uint32_t mtd_offset) {
  for (uint32_t block_offset = 0; block_offset < nand->BlockSize();
       block_offset += nand->PageSize()) {
    uint32_t actual_bytes_read = 0;
    zx_status_t res = nand->ReadPage(mtd_offset + block_offset, block_buffer->data() + block_offset,
                                     &actual_bytes_read);
    if (res != ZX_OK || actual_bytes_read != nand->PageSize()) {
      fprintf(stderr, "Unable to read page at offset %d: %s\n", 0, strerror(errno));
      return -1;
    }
  }
  return 0;
}

}  // namespace

std::unique_ptr<NandRedundantStorage> NandRedundantStorage::Create(
    std::unique_ptr<mtd::NandInterface> iface) {
  if (!iface) {
    return nullptr;
  }
  // Can't use std::make_unique due to private constructor.
  return std::unique_ptr<NandRedundantStorage>(new NandRedundantStorage(std::move(iface)));
}

NandRedundantStorage::NandRedundantStorage(std::unique_ptr<mtd::NandInterface> iface)
    : iface_(std::move(iface)) {}

zx_status_t NandRedundantStorage::WriteBuffer(const std::vector<uint8_t>& buffer,
                                              uint32_t num_copies, uint32_t* num_copies_written,
                                              bool skip_recovery_header) {
  ZX_DEBUG_ASSERT(iface_);
  ZX_ASSERT(num_copies_written);
  ZX_ASSERT(num_copies != 0);
  ZX_ASSERT(!buffer.empty());
  ZX_ASSERT_MSG(num_copies * iface_->BlockSize() <= iface_->Size(),
                "Not enough space for %d copies", num_copies);

  const uint32_t header_offset = (skip_recovery_header ? 0 : kNandRsHeaderSize);

  ZX_ASSERT_MSG(buffer.size() <= iface_->BlockSize() - header_offset, "File size too large");

  *num_copies_written = 0;

  // Allocates a full block for ease of writing. If the buffer-to-be-copied
  // crosses a page boundary, this will allow for padding of zeroes without
  // additional logic.
  std::vector<uint8_t> block_buffer(iface_->BlockSize(), 0);

  // If requested, write header into the front of the block sized buffer.
  if (!skip_recovery_header) {
    NandRsHeader header = MakeHeader(buffer);
    memcpy(block_buffer.data(), &header, kNandRsHeaderSize);
  }

  // Write contents into block sized buffer.
  memcpy(block_buffer.data() + header_offset, buffer.data(), buffer.size());

  for (uint32_t i = 0; i < num_copies; ++i) {
    uint32_t byte_offset = i * iface_->BlockSize();
    // This case can happen if there are a very large number of copies to
    // write, but is quite unlikely to happen. This scenario is outlined
    // in the header, as it is the caller's decision what to do about this.
    if (byte_offset >= iface_->Size()) {
      fprintf(stderr, "Reached end of MTD device without writing all copies\n");
      return *num_copies_written > 0 ? ZX_OK : ZX_ERR_NO_SPACE;
    }

    // Skip this block if:
    //
    // -- It's not possible to determine if the block is bad.
    // -- The block is explicitly marked as bad.
    // -- We are unable to erase the block.
    bool is_bad_block;
    if (iface_->IsBadBlock(byte_offset, &is_bad_block) != ZX_OK || is_bad_block ||
        iface_->EraseBlock(byte_offset) != ZX_OK) {
      ++num_copies;
      continue;
    }

    // If the buffer crosses a page boundary, continue writing each section
    // of the buffer, padding with zeroes until the next page boundary is
    // reached.
    bool buffer_written = true;
    for (uint32_t buffer_bytes_written = 0; buffer_bytes_written < buffer.size() + header_offset;
         buffer_bytes_written += iface_->PageSize()) {
      if (iface_->WritePage(byte_offset, block_buffer.data() + buffer_bytes_written, nullptr) !=
          ZX_OK) {
        ++num_copies;
        buffer_written = false;
        break;
      }

      // Still need to update byte offset for writing the remainder of the
      // file to this block.
      byte_offset += iface_->PageSize();
    }

    if (buffer_written) {
      (*num_copies_written)++;
    }
  }
  return ZX_OK;
}

zx_status_t NandRedundantStorage::ReadToBuffer(std::vector<uint8_t>* out_buffer,
                                               bool skip_recovery_header, size_t file_size) {
  ZX_DEBUG_ASSERT(iface_);
  ZX_ASSERT(out_buffer);

  if (skip_recovery_header && file_size <= 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::vector<uint8_t> block_buffer(iface_->BlockSize(), 0);
  for (uint32_t offset = 0; offset < iface_->Size(); offset += iface_->BlockSize()) {
    bool is_bad_block;
    zx_status_t bad_block_status = iface_->IsBadBlock(offset, &is_bad_block);
    if (bad_block_status != ZX_OK) {
      fprintf(stderr, "Error reading block status at offset %d: %s\n", offset, strerror(errno));
      return bad_block_status;
    }
    if (is_bad_block) {
      continue;
    }

    int status = ReadWholeBlock(iface_, &block_buffer, offset);
    if (status < 0) {
      continue;
    }

    uint32_t copy_offset = 0;

    if (!skip_recovery_header) {
      copy_offset = kNandRsHeaderSize;
      auto header = ReadHeader(block_buffer, iface_->BlockSize());

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
