// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/inspector/parser.h"

#include <zircon/assert.h>

#include <bitmap/raw-bitmap.h>

namespace minfs {

Superblock GetSuperblock(storage::BlockBuffer* buffer) {
  ZX_DEBUG_ASSERT(buffer->capacity() * buffer->BlockSize() >= sizeof(Superblock));
  return *reinterpret_cast<Superblock*>(buffer->Data(0));
}

bool GetBitmapElement(storage::BlockBuffer* buffer, uint64_t index) {
  ZX_DEBUG_ASSERT(index < buffer->capacity() * buffer->BlockSize() * CHAR_BIT);
  uint64_t byte_offset = index / bitmap::kBits;
  uint64_t bit_offset = index % bitmap::kBits;
  uint64_t mask = 1ULL << bit_offset;
  return static_cast<uint64_t*>(buffer->Data(0))[byte_offset] & mask;
}

Inode GetInodeElement(storage::BlockBuffer* buffer, uint64_t index) {
  ZX_DEBUG_ASSERT(index < buffer->capacity() * kMinfsInodesPerBlock);
  uint64_t inode_block = (index / kMinfsInodesPerBlock);
  uint64_t inode_block_offset = index % kMinfsInodesPerBlock;
  return static_cast<Inode*>(buffer->Data(inode_block))[inode_block_offset];
}

}  // namespace minfs
