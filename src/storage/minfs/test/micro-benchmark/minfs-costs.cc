// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minfs-costs.h"

#include <gtest/gtest.h>

// We try to manually count number of IOs issued and number of bytes transferred
// during common fs operations.
// We reduce our dependency on files outside this file so that any breaking
// change does not silently suppress degradation alarms.
// The consts in these files are redeclared so that changes to filesystem consts
// necessitates changes to this file and should force reevaluation of the perf
// impact
namespace minfs_micro_benchmark {

// Filesystem IOs that arrive at block device are in chunks of 64 blocks.
// This is NOT an issue specific to minfs.
// TODO(auradkar): Investigate where this is coming from.
constexpr uint64_t kFsIoSizeHighWatermark = 64;

constexpr uint64_t kMinfsSuperblockCopies = 2;

constexpr uint64_t kJournalSuperblock = 1;
constexpr uint64_t kJournalEntryHeaderBlocks = 1;
constexpr uint64_t kJournalEntryCommitBlocks = 1;
constexpr uint64_t kJournalEntryOverhead = kJournalEntryHeaderBlocks + kJournalEntryCommitBlocks;

// Returns number of bytes to store inode table
constexpr uint64_t InodeTableSize(const minfs::Superblock& sb) {
  return sb.inode_count * sb.inode_size;
}

// Converts FS blocks to number bytes.
uint64_t MinfsProperties::FsBlockToBytes(uint64_t blocks) const {
  return blocks * superblock_.block_size;
}

uint64_t MinfsProperties::FsBlockToBlockDeviceBlocks(uint64_t blocks) const {
  uint64_t bytes = blocks * superblock_.block_size;
  return block_device_sizes_.BytesToBlocks(bytes);
}

uint64_t MinfsProperties::FsBlockToBlockDeviceBytes(uint64_t blocks) const {
  uint64_t bytes = blocks * superblock_.block_size;
  return block_device_sizes_.BytesToBlocks(bytes);
}

uint64_t MinfsProperties::FsBytesToBlocks(uint64_t bytes) const {
  return (bytes + superblock_.block_size - 1) / superblock_.block_size;
}

// Update total_calls and bytes_transferred stats.
void MinfsProperties::AddIoStats(uint64_t total_calls, uint64_t blocks_transferred,
                                 llcpp::fuchsia::storage::metrics::wire::CallStat* out) const {
  out->success.total_calls += total_calls;
  out->success.bytes_transferred += FsBlockToBytes(blocks_transferred);
}

void MinfsProperties::AddMultipleBlocksReadCosts(uint64_t block_count,
                                                 BlockFidlMetrics* out) const {
  uint64_t total_read_calls =
      (FsBlockToBlockDeviceBlocks(block_count) + kFsIoSizeHighWatermark - 1) /
      kFsIoSizeHighWatermark;

  AddIoStats(total_read_calls, block_count, &out->read);
}

// Adds number of IOs issued and bytes transferred to write a journaled data,
// |payload_per_operation| number of blocks, over |operation_count| number of
// operations, to final locations. It also assumes that each of the block
// journaled goes to a different location leading to a different write IO. For
// now, this does not consider journal to be a ring buffer.
void MinfsProperties::AddJournalCosts(uint64_t operation_count, uint64_t payload_per_operation,
                                      BlockFidlMetrics* out) const {
  uint64_t total_write_call = 0;
  uint64_t blocks_written = 0;

  // We write to journal and then to final location
  blocks_written = 2 * operation_count * payload_per_operation;

  // Blocks written to journal are wrapped in entry.
  blocks_written += kJournalEntryOverhead;

  // Writing journal entry to journal is one write call.
  total_write_call = 1;

  // But writing to final location requires as many calls as journaled blocks.
  total_write_call += (operation_count * payload_per_operation);

  AddIoStats(total_write_call, blocks_written, &out->write);
}

void MinfsProperties::AddCleanJournalLoadCosts(BlockFidlMetrics* out) const {
  // Journal header should be read.
  AddIoStats(1, kJournalSuperblock, &out->read);

  // When filesystem is clean, nothing else should be read. But we seem to be
  // reading rest of the journal.
  // TODO(auradkar): We can avoid reading rest of the journal.
  AddMultipleBlocksReadCosts(minfs::JournalBlocks(superblock_) - kJournalSuperblock, out);
}

void MinfsProperties::AddUpdateJournalStartCost(BlockFidlMetrics* out) const {
  AddIoStats(1, kJournalSuperblock, &out->write);
}

uint64_t MinfsProperties::BitsToFsBlocks(uint64_t bits) const {
  ZX_ASSERT(superblock_.block_size > 0);
  return (bits + (superblock_.block_size * 8) - 1) / (superblock_.block_size * 8);
}

// Adds number of IOs issued and bytes transferred to read all the FS metadata
// when filesystem is in clean state.
void MinfsProperties::AddReadingCleanMetadataCosts(BlockFidlMetrics* out) const {
  // On clean mount only one superblock copy is read.
  AddIoStats(1, 1, &out->read);

  // Journal header should be read but nothing should be read or replayed if
  // filesystem is clean.
  AddCleanJournalLoadCosts(out);

  // One call for all of ibm.
  AddMultipleBlocksReadCosts(BitsToFsBlocks(superblock_.inode_count), out);

  // One call for all of abm.
  AddMultipleBlocksReadCosts(BitsToFsBlocks(superblock_.dat_block), out);

  // One for all of inode table.
  AddMultipleBlocksReadCosts(FsBytesToBlocks(InodeTableSize(superblock_)), out);
}

void MinfsProperties::AddMountCost(BlockFidlMetrics* out) const {
  // We read superblock first
  AddIoStats(1, 1, &out->read);

  // Mount brings all the metadata into memory.
  AddReadingCleanMetadataCosts(out);

  // At the end of the mount, we update dirty bit of superblock and of backup superblock.
  AddJournalCosts(1, kMinfsSuperblockCopies, out);

  // A write to the super-block.
  AddIoStats(1, 1, &out->write);

  // Updating the clean bit and oldest revision requires two flushes.
  AddIoStats(2, 0, &out->flush);
}

void MinfsProperties::AddUnmountCost(BlockFidlMetrics* out) const {
  // During unmount we clear dirty bits of superblock and of backup superblock.
  AddJournalCosts(1, kMinfsSuperblockCopies, out);

  // During unmount we write updated journal info.
  AddIoStats(1, 1, &out->write);

  // Two flushes to clear the dirty bit and one final flush to top it off
  AddIoStats(3, 0, &out->flush);
}

void MinfsProperties::AddSyncCost(BlockFidlMetrics* out, SyncKind kind) const {
  int flush_calls = 0;
  switch (kind) {
    case SyncKind::kNoTransaction:
      flush_calls = 1;
      break;
    case SyncKind::kTransactionWithNoData:
      flush_calls = 3;
      AddUpdateJournalStartCost(out);
      break;
    case SyncKind::kTransactionWithData:
      flush_calls = 4;
      AddUpdateJournalStartCost(out);
      break;
  }
  AddIoStats(flush_calls, 0, &out->flush);
}

void MinfsProperties::AddLookUpCost(BlockFidlMetrics* out) const {
  // Empty directory should have one block and read it the block
  AddIoStats(1, 1, &out->read);
}

void MinfsProperties::AddCreateCost(BlockFidlMetrics* out) const {
  // We lookup before we create.
  AddLookUpCost(out);

  // Creating a file involves
  // 1. Allocating inode
  // 2. Updating inode table
  // 3. Updating superblock
  // 4. Adding directory entry
  // 5. Updating directory inode
  // For freshly created step 2 and 5 belong to same block. So, in total 4
  // journalled block update
  AddJournalCosts(1, 4, out);
}

void MinfsProperties::AddUncachedWriteCost(uint64_t start_offset, uint64_t bytes_per_write,
                                           int write_count, BlockFidlMetrics* out) const {
  // Only writing less than a block at offset 0 is supported at the moment.
  EXPECT_LE(bytes_per_write, superblock_.block_size);
  EXPECT_EQ(start_offset, 0u);
  // A write would involve (not in that order)
  // 1. Allocating a block
  // 2. Updating inode to point to block
  // 3. Updating superblock
  // 4. Writing data
  // Step 1-3 are journalled.
  if (bytes_per_write < superblock_.BlockSize()) {
    EXPECT_LE(bytes_per_write * write_count, superblock_.block_size);
    // Here we assume that all the writes are contained within a block. So, if
    // the |write_count| is greater than 1, the block will see CoW/update.
    AddJournalCosts(write_count, 3, out);
    AddIoStats(write_count, write_count, &out->write);
  } else {
    // Here every write allocates a new block and a fresh data is written to it.
    // There is no CoW/update happens to the block.
    for (int i = 0; i < write_count; i++) {
      AddJournalCosts(1, 3, out);
      AddIoStats(1, 1, &out->write);
    }
  }
}

void MinfsProperties::AddCachedWriteCost(uint64_t start_offset, uint64_t bytes_per_write,
                                         int write_count, BlockFidlMetrics* out) const {
  // Only writing less than a block at offset 0 is supported at the moment.
  EXPECT_LE(bytes_per_write, superblock_.block_size);
  EXPECT_EQ(start_offset, 0ul);
  // A write would involve (not in that order)
  // 1. Allocating a block
  // 2. Updating inode to point to block
  // 3. Updating superblock
  // 4. Writing data
  // Step 1-3 are journalled.
  AddJournalCosts(1, 3, out);
  if (bytes_per_write < superblock_.BlockSize()) {
    EXPECT_LE(bytes_per_write * write_count, superblock_.block_size);
    // Here we assume that all the writes are contained within a block. So, if
    // the |write_count| is greater than 1, the block will see CoW/update.
    AddIoStats(1, 1, &out->write);
  } else {
    // Here every write allocates a new block and a fresh data is written to it.
    // There is no CoW/update happens to the block.
    AddIoStats(1, write_count, &out->write);
  }
}

void MinfsProperties::AddWriteCost(uint64_t start_offset, uint64_t bytes_per_write, int write_count,
                                   bool dirty_cache_enabled, BlockFidlMetrics* out) const {
  if (dirty_cache_enabled) {
    return AddCachedWriteCost(start_offset, bytes_per_write, write_count, out);
  }
  return AddUncachedWriteCost(start_offset, bytes_per_write, write_count, out);
}

}  // namespace minfs_micro_benchmark
