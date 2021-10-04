// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/block_writer.h"

#include <iostream>

#include "src/storage/volume_image/utils/block_utils.h"

namespace storage::volume_image {
fpromise::result<void, std::string> BlockWriter::Write(uint64_t offset,
                                                       cpp20::span<const uint8_t> buffer) {
  if (buffer.size() == 0) {
    return fpromise::ok();
  }

  if (offset + buffer.size() > block_count_ * block_size_) {
    return fpromise::error("BlockWriter::Write out of bounds. Offset " + std::to_string(offset) +
                           " Write Size: " + std::to_string(buffer.size()) + " with " +
                           std::to_string(block_count_) + " blocks of size " +
                           std::to_string(block_size_) +
                           " (Max Offset: " + std::to_string(block_size_ * block_count_) + ").");
  }

  // First block is unaligned.
  if (!IsOffsetBlockAligned(offset, block_size_)) {
    uint64_t block_byte_offset = GetBlockFromBytes(offset, block_size_) * block_size_;
    if (auto result = reader_->Read(block_byte_offset, block_buffer_); result.is_error()) {
      return result;
    }
    uint64_t offset_from_block_buffer = GetOffsetFromBlockStart(offset, block_size_);
    uint64_t block_bytes_to_patch =
        std::min(block_size_ - offset_from_block_buffer, static_cast<uint64_t>(buffer.size()));
    memcpy(block_buffer_.data() + offset_from_block_buffer, buffer.data(), block_bytes_to_patch);

    if (auto result = writer_->Write(block_byte_offset, block_buffer_); result.is_error()) {
      return result.take_error_result();
    }

    // We consumed all the bytes to write.
    if (block_bytes_to_patch == buffer.size()) {
      return fpromise::ok();
    }

    offset += block_bytes_to_patch;
    buffer = buffer.subspan(block_bytes_to_patch);
  }

  // offset is now aligned and at least one block.
  uint64_t aligned_block_count = GetBlockCount(offset, buffer.size(), block_size_);
  bool last_block_is_aligned = true;

  // If the buffer has trailing data, then the aligned blocks is reduced by one.
  if (buffer.size() % block_size_ != 0) {
    aligned_block_count = aligned_block_count > 0 ? aligned_block_count - 1 : 0;
    last_block_is_aligned = false;
  }

  if (aligned_block_count > 0) {
    uint64_t aligned_data_bytes = aligned_block_count * block_size_;
    if (auto result = writer_->Write(offset, buffer.subspan(0, aligned_data_bytes));
        result.is_error()) {
      return result;
    }

    // We consumed all the bytes to write.
    if (aligned_data_bytes == buffer.size()) {
      return fpromise::ok();
    }

    offset += aligned_data_bytes;
    buffer = buffer.subspan(aligned_data_bytes);
  }

  // Now write the trailing data from last block.
  if (!last_block_is_aligned) {
    if (auto result = reader_->Read(offset, block_buffer_); result.is_error()) {
      return result;
    }
    memcpy(block_buffer_.data(), buffer.data(), buffer.size());
    if (auto result = writer_->Write(offset, block_buffer_); result.is_error()) {
      return result;
    }
  }
  return fpromise::ok();
}

}  // namespace storage::volume_image
