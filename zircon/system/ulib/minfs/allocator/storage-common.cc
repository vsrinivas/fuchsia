// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <storage/buffer/block_buffer.h>

#include "allocator.h"
#include "storage.h"

namespace minfs {

namespace {

blk_t BitmapBlocksForSizeImpl(size_t size) {
  return (static_cast<blk_t>(size) + kMinfsBlockBits - 1) / kMinfsBlockBits;
}

// TODO(46781): This should not be needed after making PersistRange receive a
// buffer instead of an ambiguous WriteData.
class UnownedBuffer : public storage::BlockBuffer {
 public:
  UnownedBuffer(const void* data) : data_(reinterpret_cast<const char*>(data)) {}
  ~UnownedBuffer() {}

  // BlockBuffer interface:
  size_t capacity() const final { return 0; }
  uint32_t BlockSize() const final { return 0; }
  vmoid_t vmoid() const final { return 0; }
  zx_handle_t Vmo() const final { return ZX_HANDLE_INVALID; }
  void* Data(size_t index) final {
    return const_cast<void*>(const_cast<const UnownedBuffer*>(this)->Data(index));
  }
  const void* Data(size_t index) const final { return data_ + index * kMinfsBlockSize; }

 private:
  const char* data_;
};

}  // namespace

uint32_t AllocatorStorage::PoolBlocks() const { return BitmapBlocksForSizeImpl(PoolTotal()); }

void PersistentStorage::Load(fs::BufferedOperationsBuilder* builder, storage::BlockBuffer* data) {
  storage::Operation operation {
      .type = storage::OperationType::kRead,
      .vmo_offset = 0,
      .dev_offset = metadata_.MetadataStartBlock(),
      .length = PoolBlocks(),
  };
  builder->Add(operation, data);
}

void PersistentStorage::PersistRange(PendingWork* transaction, WriteData data, size_t index,
                                     size_t count) {
  ZX_DEBUG_ASSERT(transaction != nullptr);
  // Determine the blocks containing the first and last indices.
  blk_t first_rel_block = static_cast<blk_t>(index / kMinfsBlockBits);
  blk_t last_rel_block = static_cast<blk_t>((index + count - 1) / kMinfsBlockBits);

  // Calculate number of blocks based on the first and last blocks touched.
  blk_t block_count = last_rel_block - first_rel_block + 1;
  blk_t abs_block = metadata_.MetadataStartBlock() + first_rel_block;

  storage::Operation op = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = first_rel_block,
      .dev_offset = abs_block,
      .length = block_count,
  };

#ifdef __Fuchsia__
  transaction->EnqueueMetadata(data, op);
#else
  UnownedBuffer buffer(data);
  transaction->EnqueueMetadata(op, &buffer);
#endif
}

void PersistentStorage::PersistAllocate(PendingWork* write_transaction, size_t count) {
  ZX_DEBUG_ASSERT(write_transaction != nullptr);
  metadata_.PoolAllocate(static_cast<blk_t>(count));
  sb_->Write(write_transaction, UpdateBackupSuperblock::kNoUpdate);
}

void PersistentStorage::PersistRelease(PendingWork* write_transaction, size_t count) {
  ZX_DEBUG_ASSERT(write_transaction != nullptr);
  metadata_.PoolRelease(static_cast<blk_t>(count));
  sb_->Write(write_transaction, UpdateBackupSuperblock::kNoUpdate);
}

// Static.
blk_t PersistentStorage::BitmapBlocksForSize(size_t size) { return BitmapBlocksForSizeImpl(size); }

}  // namespace minfs
