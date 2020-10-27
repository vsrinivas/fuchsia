// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_TEST_MICRO_BENCHMARK_MINFS_COSTS_H_
#define SRC_STORAGE_MINFS_TEST_MICRO_BENCHMARK_MINFS_COSTS_H_

#include <fs-management/mount.h>

#include "block-device-utils.h"
#include "src/storage/minfs/format.h"

namespace minfs_micro_benchmanrk {

class MinfsProperties {
 public:
  MinfsProperties(BlockDeviceSizes block_device_sizes, disk_format_t format,
                  mkfs_options_t mkfs_options, minfs::Superblock superblock, const char* mount_path)
      : block_device_sizes_(block_device_sizes),
        format_(format),
        mkfs_options_(mkfs_options),
        superblock_(superblock),
        mount_path_(mount_path) {}

  // Adds to |out| the cost to mount a clean, freshly created, empty filesystem.
  void AddMountCost(BlockFidlMetrics* out) const;

  // Adds to |out| the cost to unmount a filesystem.
  void AddUnmountCost(BlockFidlMetrics* out) const;

  // Adds to |out| the cost to sync a filesystem.
  void AddSyncCost(BlockFidlMetrics* out, bool update_journal_start = false) const;

  // Adds to |out| the cost to lookup an entry in an empty root directory.
  void AddLookUpCost(BlockFidlMetrics* out) const;

  // Adds to |out| the cost to create a regular file in emtry root directory.
  void AddCreateCost(BlockFidlMetrics* out) const;

  // Adds tpo |out| the cost to write |size| bytes at |offset| to a zero sized
  // regular file.
  void AddWriteCost(uint64_t offset, uint64_t size, BlockFidlMetrics* out) const;

  const BlockDeviceSizes& DeviceSizes() const { return block_device_sizes_; }
  const disk_format_t& DiskFormat() const { return format_; }
  const mkfs_options_t& MkfsOptions() const { return mkfs_options_; }
  const minfs::Superblock& Superblock() const { return superblock_; }
  void SetSuperblock(const minfs::Superblock& src) {
    memcpy(&superblock_, &src, sizeof(superblock_));
  }

  const char* MountPath() const { return mount_path_; }

 private:
  // Converts FS blocks to number bytes.
  uint64_t FsBlockToBytes(uint64_t blocks) const;

  uint64_t FsBlockToBlockDeviceBlocks(uint64_t blocks) const;

  uint64_t FsBlockToBlockDeviceBytes(uint64_t blocks) const;

  uint64_t FsBytesToBlocks(uint64_t bytes) const;

  uint64_t BitsToFsBlocks(uint64_t bits) const;

  // Update total_calls and bytes_transferrd stats.
  void AddIoStats(uint64_t total_calls, uint64_t blocks_transferred,
                  fuchsia_storage_metrics_CallStat* out) const;

  void AddMultipleBlocksReadCosts(uint64_t block_count, BlockFidlMetrics* out) const;

  // Adds number of IOs issued and bytes transferred to write a journaled data, |payload| number of
  // blocks, to final locations. It also assumes that each of the block journaled goes to a
  // different location leading to a different write IO. For now, this does not consider journal to
  // be a ring buffer.
  void AddJournalCosts(uint64_t payload, BlockFidlMetrics* out) const;

  void AddCleanJournalLoadCosts(BlockFidlMetrics* out) const;

  void AddUpdateJournalStartCost(BlockFidlMetrics* out) const;

  // Adds number of IOs issued and bytes transferred to read all the FS metadata
  // when filesystem is in clean state.
  void AddReadingCleanMetadataCosts(BlockFidlMetrics* out) const;

  BlockDeviceSizes block_device_sizes_;
  disk_format_t format_;
  mkfs_options_t mkfs_options_;
  minfs::Superblock superblock_;
  const char* mount_path_;
};

}  // namespace minfs_micro_benchmanrk

#endif  // SRC_STORAGE_MINFS_TEST_MICRO_BENCHMARK_MINFS_COSTS_H_
