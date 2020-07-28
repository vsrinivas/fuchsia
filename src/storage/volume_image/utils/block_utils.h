// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_BLOCK_UTILS_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_BLOCK_UTILS_H_

#include <cstdint>
#include <tuple>

#include <fbl/algorithm.h>

namespace storage::volume_image {

// Returns the byte offset of |byte_offset| relative to the start of the block that contains
// |byte_offset|.
constexpr uint64_t GetOffsetFromBlockStart(uint64_t byte_offset, uint64_t block_size) {
  return byte_offset % block_size;
}

// Returns the trailing bytes on the last block for a sequence of |byte_counts|.
constexpr uint64_t GetRemainderFromBlock(uint64_t byte_count, uint64_t block_size) {
  return block_size - (byte_count + block_size - 1) % block_size - 1;
}

// Returns the offset in number of blocks of |block_size| for |byte_offset|.
constexpr uint64_t GetBlockFromBytes(uint64_t byte_offset, uint64_t block_size) {
  return byte_offset / block_size;
}

// Returns the number of blocks required to write |byte_count| from |byte_offset| with |block_size|.
constexpr uint64_t GetBlockCount(uint64_t byte_offset, uint64_t byte_count, uint64_t block_size) {
  if (byte_count == 0) {
    return 0;
  }
  uint64_t block_number = GetBlockFromBytes(byte_offset, block_size);
  uint64_t last_block = fbl::round_up(byte_offset + byte_count, block_size) / block_size;
  return last_block - block_number;
}

// Retruns true if the |byte_offset| is block aligned.
constexpr bool IsOffsetBlockAligned(uint64_t byte_offset, uint64_t block_size) {
  return GetOffsetFromBlockStart(byte_offset, block_size) == 0;
}

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_BLOCK_UTILS_H_
