// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/transaction_limits.h"

#include <zircon/assert.h>
#include <zircon/errors.h>

#include <algorithm>

#include <fbl/algorithm.h>

#include "src/lib/storage/vfs/cpp/journal/format.h"
#include "src/storage/minfs/format.h"

namespace minfs {

blk_t GetBlockBitmapBlocks(const Superblock& info) {
  ZX_DEBUG_ASSERT(info.ino_block >= info.abm_block);
  blk_t bitmap_blocks = info.ino_block - info.abm_block;

  if (info.flags & kMinfsFlagFVM) {
    const blk_t kBlocksPerSlice = static_cast<blk_t>(info.slice_size / info.BlockSize());
    bitmap_blocks = info.abm_slices * kBlocksPerSlice;
  }

  return bitmap_blocks;
}

zx::status<blk_t> GetRequiredBlockCount(size_t offset, size_t length, uint32_t block_size) {
  if (block_size != kMinfsBlockSize) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (length == 0) {
    // Return early if no data needs to be written.
    return zx::ok(0);
  }

  // Determine which range of direct blocks will be accessed given offset and length,
  // and add to total.
  blk_t first_direct = static_cast<blk_t>(offset / block_size);
  blk_t last_direct = static_cast<blk_t>((offset + length - 1) / block_size);
  blk_t reserve_blocks = last_direct - first_direct + 1;

  if (last_direct < kMinfsDirect) {
    return zx::ok(reserve_blocks);
  }

  // If direct blocks go into indirect range, adjust the indices accordingly.
  first_direct = std::max(first_direct, kMinfsDirect) - kMinfsDirect;
  last_direct -= kMinfsDirect;

  // Calculate indirect blocks containing first and last direct blocks, and add to total.
  blk_t first_indirect = first_direct / kMinfsDirectPerIndirect;
  blk_t last_indirect = last_direct / kMinfsDirectPerIndirect;
  reserve_blocks += last_indirect - first_indirect + 1;

  if (last_indirect >= kMinfsIndirect) {
    // If indirect blocks go into doubly indirect range, adjust the indices accordingly.
    first_indirect = std::max(first_indirect, kMinfsIndirect) - kMinfsIndirect;
    last_indirect -= kMinfsIndirect;

    // Calculate doubly indirect blocks containing first/last indirect blocks,
    // and add to total
    blk_t first_dindirect = first_indirect / kMinfsDirectPerIndirect;
    blk_t last_dindirect = last_indirect / kMinfsDirectPerIndirect;
    reserve_blocks += last_dindirect - first_dindirect + 1;

    if (last_dindirect >= kMinfsDoublyIndirect) {
      // We cannot allocate blocks which exceed the doubly indirect range.
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
  }

  return zx::ok(reserve_blocks);
}

TransactionLimits::TransactionLimits(const Superblock& info) : block_size_(info.BlockSize()) {
  CalculateDataBlocks();
  CalculateIntegrityBlocks(GetBlockBitmapBlocks(info));
}

void TransactionLimits::CalculateDataBlocks() {
  // If we ever increase the number of doubly indirect blocks, we will need to update this offset
  // to be 1 byte before the end of the first doubly indirect block.
  const blk_t offset =
      (kMinfsDirect + (kMinfsIndirect * kMinfsDirectPerIndirect)) * BlockSize() - 1;

  // This calculation ignores the fact that directory size is capped at |kMinfsMaxDirectorySize|,
  // because following that constraint makes it a little harder to predict where the most
  // significant cross-block write would be. This means we may overestimate the maximum number of
  // directory blocks by some amount, but this is better than an understimate.
  blk_t max_directory_blocks =
      GetRequiredBlockCount(offset, kMinfsMaxDirentSize, BlockSize()).value();

  max_data_blocks_ = GetRequiredBlockCount(offset, kMaxWriteBytes, BlockSize()).value();

  blk_t direct_blocks = (fbl::round_up(kMaxWriteBytes, BlockSize()) / BlockSize()) + 1;
  blk_t max_indirect_blocks = max_data_blocks_ - direct_blocks;

  max_meta_data_blocks_ = std::max(max_directory_blocks, max_indirect_blocks);
}

void TransactionLimits::CalculateIntegrityBlocks(blk_t block_bitmap_blocks) {
  max_entry_data_blocks_ = kMaxSuperblockBlocks + kMaxInodeBitmapBlocks + block_bitmap_blocks +
                           kMaxInodeTableBlocks + max_meta_data_blocks_;

  // Ensure we have enough space to fit all the block numbers that may be updated in one
  // transaction. This may spill over into multiple blocks.
  blk_t header_blocks = 1;
  if (max_entry_data_blocks_ > fs::kMaxBlockDescriptors) {
    header_blocks += fbl::round_up((max_entry_data_blocks_ - fs::kMaxBlockDescriptors),
                                   kMinfsDirectPerIndirect) /
                     kMinfsDirectPerIndirect;
  }

  // For revocation records, we need to know the maximum number of metadata blocks within the
  // data section of Minfs that can be deleted within one operation. This is either a directory
  // vnode's maximum possible number of data blocks + indirect blocks, or a data vnode's maximum
  // possible number of indirect blocks.
  blk_t maximum_directory_blocks =
      GetRequiredBlockCount(0, kMinfsMaxDirectorySize, BlockSize()).value();
  blk_t maximum_indirect_blocks = kMinfsIndirect + kMinfsDoublyIndirect * kMinfsDirectPerIndirect;
  blk_t revocation_blocks =
      fbl::round_up(std::max(maximum_directory_blocks, maximum_indirect_blocks),
                    kMinfsDirectPerIndirect) /
      kMinfsDirectPerIndirect;

  blk_t commit_blocks = 1;

  max_entry_blocks_ = header_blocks + revocation_blocks + max_entry_data_blocks_ + commit_blocks;
  min_integrity_blocks_ = max_entry_blocks_ + kJournalMetadataBlocks + kBackupSuperblockBlocks;
  rec_integrity_blocks_ = std::max(min_integrity_blocks_, kDefaultJournalBlocks);
}

}  // namespace minfs
