// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

#include <zircon/assert.h>

#include <bitmap/raw-bitmap.h>

namespace blobfs {

Superblock GetSuperblock(storage::BlockBuffer* buffer) {
  ZX_DEBUG_ASSERT(buffer->capacity() * buffer->BlockSize() >= sizeof(Superblock));
  return *reinterpret_cast<Superblock*>(buffer->Data(0));
}

bool GetBitmapElement(storage::BlockBuffer* buffer, uint64_t index) {
  ZX_DEBUG_ASSERT(index < buffer->capacity() * kBlobfsBlockBits);
  uint64_t byte_offset = index / bitmap::kBits;
  uint64_t bit_offset = index % bitmap::kBits;
  uint64_t mask = 1ULL << bit_offset;
  return static_cast<uint64_t*>(buffer->Data(0))[byte_offset] & mask;
}

Inode GetInodeElement(storage::BlockBuffer* buffer, uint64_t index) {
  ZX_DEBUG_ASSERT(index < buffer->capacity() * kBlobfsInodesPerBlock);
  uint64_t inode_block = (index / kBlobfsInodesPerBlock);
  uint64_t inode_block_offset = index % kBlobfsInodesPerBlock;
  return static_cast<Inode*>(buffer->Data(inode_block))[inode_block_offset];
}

void WriteBitmapElement(storage::BlockBuffer* buffer, bool value, uint64_t index) {
  ZX_DEBUG_ASSERT(index < buffer->capacity() * kBlobfsBlockBits);
  uint64_t byte_offset = index / bitmap::kBits;
  uint64_t bit_offset = index % bitmap::kBits;
  auto* data = static_cast<uint64_t*>(buffer->Data(0));
  // Clear then set bit.
  data[byte_offset] =
      (data[byte_offset] & ~(1ULL << bit_offset)) | (static_cast<uint64_t>(value) << bit_offset);
}

void WriteInodeElement(storage::BlockBuffer* buffer, Inode inode, uint64_t index) {
  ZX_DEBUG_ASSERT(index < buffer->capacity() * kBlobfsInodesPerBlock);
  uint64_t inode_block = (index / kBlobfsInodesPerBlock);
  uint64_t inode_block_offset = index % kBlobfsInodesPerBlock;
  static_cast<Inode*>(buffer->Data(inode_block))[inode_block_offset] = inode;
}

}  // namespace blobfs
