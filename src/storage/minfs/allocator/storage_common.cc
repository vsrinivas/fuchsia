// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <storage/buffer/block_buffer.h>

#include "src/storage/minfs/allocator/allocator.h"
#include "src/storage/minfs/allocator/storage.h"
#include "src/storage/minfs/unowned_vmo_buffer.h"

namespace minfs {

static blk_t BitmapBlocksForSizeImpl(size_t size) {
  return (static_cast<blk_t>(size) + kMinfsBlockBits - 1) / kMinfsBlockBits;
}

uint32_t AllocatorStorage::PoolBlocks() const { return BitmapBlocksForSizeImpl(PoolTotal()); }

void PersistentStorage::Load(fs::BufferedOperationsBuilder* builder, storage::BlockBuffer* data) {
  storage::Operation operation{
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

  storage::Operation operation = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = first_rel_block,
      .dev_offset = abs_block,
      .length = block_count,
  };

#ifdef __Fuchsia__
  zx::unowned_vmo vmo(data);
  UnownedVmoBuffer buffer(vmo);
#else
  fs::internal::BorrowedBuffer buffer(data);
#endif
  transaction->EnqueueMetadata(operation, &buffer);
}

void PersistentStorage::PersistAllocate(PendingWork* write_transaction, size_t count) {
  ZX_DEBUG_ASSERT(write_transaction != nullptr);
  metadata_.PoolAllocate(static_cast<blk_t>(count));
}

void PersistentStorage::PersistRelease(PendingWork* write_transaction, size_t count) {
  ZX_DEBUG_ASSERT(write_transaction != nullptr);
  metadata_.PoolRelease(static_cast<blk_t>(count));
}

// Static.
blk_t PersistentStorage::BitmapBlocksForSize(size_t size) { return BitmapBlocksForSizeImpl(size); }

}  // namespace minfs
