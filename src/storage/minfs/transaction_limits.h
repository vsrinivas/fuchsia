// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_TRANSACTION_LIMITS_H_
#define SRC_STORAGE_MINFS_TRANSACTION_LIMITS_H_

#include <lib/zx/status.h>

#include "src/storage/minfs/format.h"

namespace minfs {

// TODO(fxbug.dev/32627): Break up transactions into chunks so that journal size
//                is not dependent on block bitmap size.

// Calculates and returns the maximum number of block bitmap blocks, based on |info_|.
blk_t GetBlockBitmapBlocks(const Superblock& info);

// Returns the required number of blocks for a write at the given |offset|
// and |length|.
zx::status<blk_t> GetRequiredBlockCount(size_t offset, size_t length, size_t block_size);

// Calculates and tracks the number of Minfs metadata / data blocks that can be modified within one
// transaction, as well as the corresponding Journal sizes.
// Once we can grow the block bitmap, we will need to be able to recalculate these limits.
class TransactionLimits {
 public:
  explicit TransactionLimits(const Superblock& info);

  // Returns the maximum number of metadata blocks that we expect to be modified in the data
  // section within one transaction. For data vnodes, based on a max write size of 64kb, this is
  // currently expected to be 3 indirect blocks (would be 4 with the introduction of more doubly
  // indirect blocks). For directories, with a max dirent size of 268b, this is expected to be 5
  // blocks.
  blk_t GetMaximumMetaDataBlocks() const { return max_meta_data_blocks_; }

  // Returns the maximum number of data blocks (including indirects) that we expect to be
  // modified within one transaction. Based on a max write size of 64kb, this is currently
  // expected to be 9 direct blocks + 3 indirect blocks = 11 total blocks. With the addition of
  // more doubly indirect blocks, this would increase to 4 indirect blocks for a total of 12
  // blocks.
  blk_t GetMaximumDataBlocks() const { return max_data_blocks_; }

  // Returns the maximum number of data blocks that can be included in a journal entry,
  // i.e. the total number of blocks that can be held in a transaction enqueued to the journal.
  blk_t GetMaximumEntryDataBlocks() const { return max_entry_data_blocks_; }

  // Returns the total number of blocks required for the maximum size journal entry.
  blk_t GetMaximumEntryBlocks() const { return max_entry_blocks_; }

  // Returns the minimum number of blocks required to create a journal guaranteed large enough to
  // hold at least a single journal entry of maximum size, as well as the backup superblock.
  blk_t GetMinimumIntegrityBlocks() const { return min_integrity_blocks_; }

  // Returns the ideal number of blocks to allocate to the integrity section, provided enough space
  // is available.
  blk_t GetRecommendedIntegrityBlocks() const { return rec_integrity_blocks_; }

  // Maximum number of superblock blocks that can be modified within one transaction.
  // Since there are 2 superblocks (original and backup),
  // there can be 2 blocks updated on each transaction.
  static constexpr blk_t kMaxSuperblockBlocks = 2;

  // TODO(planders): Enforce all of the following limits.
  //                 (Perhaps by tracking modified counts within the Transaction).
  // Maximum number of inode bitmap blocks that can be modified within one transaction.
  // A maximum of 1 inode can be created or deleted during a single transaction.
  static constexpr blk_t kMaxInodeBitmapBlocks = 1;

  // Maximum number of inode table blocks that can be modified within one transaction.
  // No more than 2 inodes will be modified during a single transaction.
  // (In the case of Create, the parent directory and the child inode will be modified.)
  static constexpr blk_t kMaxInodeTableBlocks = 2;

  // The largest amount of data that Write() should able to process at once. This is currently
  // constrainted by external factors to (1 << 13), but with the switch to FIDL we expect
  // incoming requests to be NO MORE than (1 << 16). Even so, we should update Write() to handle
  // cases beyond this.
  // TODO(planders): Internally break up large write requests so they fit within this constraint.
  static constexpr size_t kMaxWriteBytes = (1 << 16);

  // Number of metadata blocks required for the whole journal - 1 Superblock.
  static constexpr blk_t kJournalMetadataBlocks = 1;

  // Default number of blocks which should be allocated to the journal, if the minimum
  // requirement does not exceed it.
  static constexpr blk_t kDefaultJournalBlocks = 256;

 private:
  // Calculates the maximum number of data and metadata blocks that can be updated during a
  // single transaction.
  void CalculateDataBlocks();

  // Calculates the maximum journal entry size and the minimum size required for the integrity
  // section of Minfs (journal + backup superblock).
  void CalculateIntegrityBlocks(blk_t block_bitmap_blocks);

  uint32_t BlockSize() const {
    // Either intentionally or unintenttionally, we do not want to change block
    // size to anything other than kMinfsBlockSize yet. This is because changing
    // block size might lead to format change and also because anything other
    // than 8k is not well tested. So assert when we find block size other
    // than 8k.
    ZX_ASSERT(block_size_ == kMinfsBlockSize);
    return block_size_;
  }
  uint32_t block_size_ = {};
  blk_t max_meta_data_blocks_;
  blk_t max_data_blocks_;
  blk_t max_entry_data_blocks_;
  blk_t max_entry_blocks_;
  blk_t min_integrity_blocks_;
  blk_t rec_integrity_blocks_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_TRANSACTION_LIMITS_H_
