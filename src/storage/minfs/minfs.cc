// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/minfs.h"

#include <fcntl.h>
#include <inttypes.h>
#include <lib/cksum.h>
#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <iomanip>
#include <limits>
#include <memory>
#include <utility>

#include <bitmap/raw-bitmap.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <safemath/safe_math.h>

#include "src/lib/storage/vfs/cpp/journal/format.h"
#include "src/lib/storage/vfs/cpp/journal/initializer.h"
#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/storage/minfs/allocator/allocator_reservation.h"
#include "src/storage/minfs/file.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs_private.h"
#include "src/storage/minfs/writeback.h"

#ifdef __Fuchsia__
#include <fidl/fuchsia.minfs/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>

#include <fbl/auto_lock.h>
#include <storage/buffer/owned_vmoid.h>

#include "sdk/lib/sys/cpp/service_directory.h"
#include "src/lib/storage/vfs/cpp/journal/header_view.h"
#include "src/lib/storage/vfs/cpp/journal/journal.h"
#include "src/lib/storage/vfs/cpp/journal/replay.h"
#include "src/lib/storage/vfs/cpp/metrics/events.h"
#include "src/storage/fvm/client.h"
#endif

namespace minfs {
namespace {

#ifdef __Fuchsia__
// Deletes all known slices from a MinFS Partition.
void FreeSlices(const Superblock* info, block_client::BlockDevice* device) {
  if ((info->flags & kMinfsFlagFVM) == 0) {
    return;
  }
  extend_request_t request;
  const size_t kBlocksPerSlice = info->slice_size / info->BlockSize();
  if (info->ibm_slices) {
    request.length = info->ibm_slices;
    request.offset = kFVMBlockInodeBmStart / kBlocksPerSlice;
    device->VolumeShrink(request.offset, request.length);
  }
  if (info->abm_slices) {
    request.length = info->abm_slices;
    request.offset = kFVMBlockDataBmStart / kBlocksPerSlice;
    device->VolumeShrink(request.offset, request.length);
  }
  if (info->ino_slices) {
    request.length = info->ino_slices;
    request.offset = kFVMBlockInodeStart / kBlocksPerSlice;
    device->VolumeShrink(request.offset, request.length);
  }
  if (info->dat_slices) {
    request.length = info->dat_slices;
    request.offset = kFVMBlockDataStart / kBlocksPerSlice;
    device->VolumeShrink(request.offset, request.length);
  }
}

// Checks all slices against the block device. May shrink the partition.
zx::status<> CheckSlices(const Superblock& info, size_t blocks_per_slice,
                         block_client::BlockDevice* device, bool repair_slices) {
  fuchsia_hardware_block_volume_VolumeManagerInfo manager_info;
  fuchsia_hardware_block_volume_VolumeInfo volume_info;
  zx_status_t status = device->VolumeGetInfo(&manager_info, &volume_info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "unable to query FVM :" << status;
    return zx::error(ZX_ERR_UNAVAILABLE);
  }

  if (info.slice_size != manager_info.slice_size) {
    FX_LOGS(ERROR) << "slice size " << info.slice_size << " did not match expected size "
                   << manager_info.slice_size;
    return zx::error(ZX_ERR_BAD_STATE);
  }

  size_t expected_count[4];
  expected_count[0] = info.ibm_slices;
  expected_count[1] = info.abm_slices;
  expected_count[2] = info.ino_slices;
  expected_count[3] = info.dat_slices;

  query_request_t request;
  request.count = 4;
  request.vslice_start[0] = kFVMBlockInodeBmStart / blocks_per_slice;
  request.vslice_start[1] = kFVMBlockDataBmStart / blocks_per_slice;
  request.vslice_start[2] = kFVMBlockInodeStart / blocks_per_slice;
  request.vslice_start[3] = kFVMBlockDataStart / blocks_per_slice;

  fuchsia_hardware_block_volume_VsliceRange
      ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
  size_t ranges_count;

  status = device->VolumeQuerySlices(request.vslice_start, request.count, ranges, &ranges_count);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "unable to query FVM: " << status;
    return zx::error(ZX_ERR_UNAVAILABLE);
  }

  if (ranges_count != request.count) {
    FX_LOGS(ERROR) << "requested FVM range :" << request.count
                   << " does not match received: " << ranges_count;
    return zx::error(ZX_ERR_BAD_STATE);
  }

  for (uint32_t i = 0; i < request.count; i++) {
    size_t minfs_count = expected_count[i];
    size_t fvm_count = ranges[i].count;

    if (!ranges[i].allocated || fvm_count < minfs_count) {
      // Currently, since Minfs can only grow new slices (except for the one instance below), it
      // should not be possible for the FVM to report a slice size smaller than what is reported by
      // Minfs. In this case, automatically fail without trying to resolve the situation, as it is
      // possible that Minfs structures are allocated in the slices that have been lost.
      FX_LOGS(ERROR) << "mismatched slice count";
      return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
    }

    if (repair_slices && fvm_count > minfs_count) {
      // If FVM reports more slices than we expect, try to free remainder.
      extend_request_t shrink;
      shrink.length = fvm_count - minfs_count;
      shrink.offset = request.vslice_start[i] + minfs_count;
      if ((status = device->VolumeShrink(shrink.offset, shrink.length)) != ZX_OK) {
        FX_LOGS(ERROR) << "Unable to shrink to expected size, status: " << status;
        return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
      }
    }
  }
  return zx::ok();
}

// Setups the superblock based on the mount options and the underlying device.
// It can be called when not loaded on top of FVM, in which case this function
// will do nothing.
zx::status<> CreateFvmData(const MountOptions& options, Superblock* info,
                           block_client::BlockDevice* device) {
  fuchsia_hardware_block_volume_VolumeManagerInfo manager_info;
  fuchsia_hardware_block_volume_VolumeInfo volume_info;
  if (device->VolumeGetInfo(&manager_info, &volume_info) != ZX_OK) {
    return zx::ok();
  }

  info->slice_size = static_cast<uint32_t>(manager_info.slice_size);
  SetMinfsFlagFvm(*info);

  if (info->slice_size % info->BlockSize()) {
    FX_LOGS(ERROR) << "minfs mkfs: Slice size not multiple of minfs block: " << info->slice_size;
    return zx::error(ZX_ERR_IO_INVALID);
  }

  const size_t kBlocksPerSlice = info->slice_size / info->BlockSize();
  zx_status_t status = fvm::ResetAllSlices(device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "minfs mkfs: Failed to reset FVM slices: " << status;
    return zx::error(status);
  }

  extend_request_t request;

  // Inode allocation bitmap.
  info->ibm_slices = 1;
  request.offset = kFVMBlockInodeBmStart / kBlocksPerSlice;
  request.length = info->ibm_slices;
  if (status = device->VolumeExtend(request.offset, request.length); status != ZX_OK) {
    FX_LOGS(ERROR) << "minfs mkfs: Failed to allocate inode bitmap: " << status;
    return zx::error(status);
  }

  // Data block allocation bitmap. Currently once slice should be enough for many more inodes than
  // we currently reserve (this is validated with an assertion below).
  info->abm_slices = 1;
  request.offset = kFVMBlockDataBmStart / kBlocksPerSlice;
  request.length = info->abm_slices;
  if (status = device->VolumeExtend(request.offset, request.length); status != ZX_OK) {
    FX_LOGS(ERROR) << "minfs mkfs: Failed to allocate data bitmap: " << status;
    return zx::error(status);
  }

  // Inode slice: Compute the number required to contain at least the default number of inodes.
  auto inode_blocks = (kMinfsDefaultInodeCount + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
  info->ino_slices = static_cast<uint32_t>((inode_blocks + kBlocksPerSlice - 1) / kBlocksPerSlice);
  request.offset = kFVMBlockInodeStart / kBlocksPerSlice;
  request.length = info->ino_slices;
  if (status = device->VolumeExtend(request.offset, request.length); status != ZX_OK) {
    FX_LOGS(ERROR) << "minfs mkfs: Failed to allocate inode table: " << status;
    return zx::error(status);
  }

  // The inode bitmap should be big enough to hold all the inodes we reserved. If this triggers we
  // need to write logic to compute the proper ibm_slices size.
  ZX_DEBUG_ASSERT(info->ibm_slices * info->slice_size * 8 >=
                  info->ino_slices * kBlocksPerSlice * kMinfsInodesPerBlock);

  // Journal.
  TransactionLimits limits(*info);
  blk_t journal_blocks = limits.GetRecommendedIntegrityBlocks();
  request.length = fbl::round_up(journal_blocks, kBlocksPerSlice) / kBlocksPerSlice;
  request.offset = kFVMBlockJournalStart / kBlocksPerSlice;
  if (status = device->VolumeExtend(request.offset, request.length); status != ZX_OK) {
    FX_LOGS(ERROR) << "minfs mkfs: Failed to allocate journal blocks: " << status;
    return zx::error(status);
  }
  info->integrity_slices = static_cast<blk_t>(request.length);

  // Data.
  ZX_ASSERT(options.fvm_data_slices > 0);
  request.length = options.fvm_data_slices;
  request.offset = kFVMBlockDataStart / kBlocksPerSlice;
  if (status = device->VolumeExtend(request.offset, request.length); status != ZX_OK) {
    FX_LOGS(ERROR) << "minfs mkfs: Failed to allocate data blocks: " << status;
    return zx::error(status);
  }
  info->dat_slices = options.fvm_data_slices;

  return zx::ok();
}
#endif

// Verifies that the allocated slices are sufficient to hold the allocated data
// structures of the filesystem.
zx::status<> VerifySlicesSize(const Superblock& info, const TransactionLimits& limits,
                              size_t blocks_per_slice) {
  size_t ibm_blocks_needed = (info.inode_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
  size_t ibm_blocks_allocated = info.ibm_slices * blocks_per_slice;
  if (ibm_blocks_needed > ibm_blocks_allocated) {
    FX_LOGS(ERROR) << "Not enough slices for inode bitmap";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (ibm_blocks_allocated + info.ibm_block >= info.abm_block) {
    FX_LOGS(ERROR) << "Inode bitmap collides into block bitmap";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  size_t abm_blocks_needed = (info.block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
  size_t abm_blocks_allocated = info.abm_slices * blocks_per_slice;
  if (abm_blocks_needed > abm_blocks_allocated) {
    FX_LOGS(ERROR) << "Not enough slices for block bitmap";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (abm_blocks_allocated + info.abm_block >= info.ino_block) {
    FX_LOGS(ERROR) << "Block bitmap collides with inode table";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  size_t ino_blocks_needed = (info.inode_count + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
  size_t ino_blocks_allocated = info.ino_slices * blocks_per_slice;
  if (ino_blocks_needed > ino_blocks_allocated) {
    FX_LOGS(ERROR) << "Not enough slices for inode table";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (ino_blocks_allocated + info.ino_block >= info.integrity_start_block) {
    FX_LOGS(ERROR) << "Inode table collides with data blocks";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  size_t journal_blocks_needed = limits.GetMinimumIntegrityBlocks();
  size_t journal_blocks_allocated = info.integrity_slices * blocks_per_slice;
  if (journal_blocks_needed > journal_blocks_allocated) {
    FX_LOGS(ERROR) << "Not enough slices for journal";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (journal_blocks_allocated + info.integrity_start_block > info.dat_block) {
    FX_LOGS(ERROR) << "Journal collides with data blocks";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  size_t dat_blocks_needed = info.block_count;
  size_t dat_blocks_allocated = info.dat_slices * blocks_per_slice;
  if (dat_blocks_needed > dat_blocks_allocated) {
    FX_LOGS(ERROR) << "Not enough slices for data blocks";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (dat_blocks_allocated + info.dat_block > std::numeric_limits<blk_t>::max()) {
    FX_LOGS(ERROR) << "Data blocks overflow blk_t";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (dat_blocks_needed <= 1) {
    FX_LOGS(ERROR) << "Not enough data blocks";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::ok();
}

// Fuses "reading the superblock from storage" with "correcting if it is wrong".
zx::status<Superblock> LoadSuperblockWithRepair(Bcache* bc, bool repair) {
  auto info_or = LoadSuperblock(bc);
  if (info_or.is_error()) {
    if (!repair) {
      FX_LOGS(ERROR) << "Cannot load superblock; not attempting to repair";
      return info_or.take_error();
    }
    FX_LOGS(WARNING) << "Attempting to repair superblock";

#ifdef __Fuchsia__
    info_or = RepairSuperblock(bc, bc->device(), bc->Maxblk());
    if (info_or.is_error()) {
      FX_LOGS(ERROR) << "Unable to repair corrupt filesystem.";
      return info_or.take_error();
    }
#else
    return zx::error(ZX_ERR_NOT_SUPPORTED);
#endif
  }

  return info_or;
}

#ifdef __Fuchsia__

// Replays the journal and reloads the superblock (it may have been present in the journal).
//
// |info| is both an input and output parameter; it may be overwritten.
zx::status<fs::JournalSuperblock> ReplayJournalReloadSuperblock(Bcache* bc, Superblock* info) {
  auto journal_block_or = ReplayJournal(bc, *info);
  if (journal_block_or.is_error()) {
    FX_LOGS(ERROR) << "Cannot replay journal";
    return journal_block_or.take_value();
  }
  // Re-load the superblock after replaying the journal.
  auto new_info_or = LoadSuperblock(bc);
  if (new_info_or.is_error()) {
    return new_info_or.take_error();
  }

  *info = std::move(new_info_or.value());
  return journal_block_or;
}

#endif

}  // namespace

zx_time_t GetTimeUTC() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  zx_time_t time = zx_time_add_duration(ZX_SEC(ts.tv_sec), ts.tv_nsec);
  return time;
}

void DumpInfo(const Superblock& info) {
  FX_LOGS(DEBUG) << "magic0:  " << std::setw(10) << info.magic0;
  FX_LOGS(DEBUG) << "magic1:  " << std::setw(10) << info.magic1;
  FX_LOGS(DEBUG) << "major version:  " << std::setw(10) << info.major_version;
  FX_LOGS(DEBUG) << "data blocks:  " << std::setw(10) << info.block_count << " (size "
                 << info.block_size << ")";
  FX_LOGS(DEBUG) << "inodes:  " << std::setw(10) << info.inode_count << " (size " << info.inode_size
                 << ")";
  FX_LOGS(DEBUG) << "allocated blocks  @ " << std::setw(10) << info.alloc_block_count;
  FX_LOGS(DEBUG) << "allocated inodes  @ " << std::setw(10) << info.alloc_inode_count;
  FX_LOGS(DEBUG) << "inode bitmap @ " << std::setw(10) << info.ibm_block;
  FX_LOGS(DEBUG) << "alloc bitmap @ " << std::setw(10) << info.abm_block;
  FX_LOGS(DEBUG) << "inode table  @ " << std::setw(10) << info.ino_block;
  FX_LOGS(DEBUG) << "integrity start block  @ " << std::setw(10) << info.integrity_start_block;
  FX_LOGS(DEBUG) << "data blocks  @ " << std::setw(10) << info.dat_block;
  FX_LOGS(DEBUG) << "FVM-aware: " << ((info.flags & kMinfsFlagFVM) ? "YES" : "NO");
  FX_LOGS(DEBUG) << "checksum:  " << std::setw(10) << info.checksum;
  FX_LOGS(DEBUG) << "generation count:  " << std::setw(10) << info.generation_count;
  FX_LOGS(DEBUG) << "oldest_minor_version:  " << std::setw(10) << info.oldest_minor_version;
  FX_LOGS(DEBUG) << "slice_size: " << info.slice_size;
  FX_LOGS(DEBUG) << "ibm_slices: " << info.ibm_slices;
  FX_LOGS(DEBUG) << "abm_slices: " << info.abm_slices;
  FX_LOGS(DEBUG) << "ino_slices: " << info.ino_slices;
  FX_LOGS(DEBUG) << "integrity_slices: " << info.integrity_slices;
  FX_LOGS(DEBUG) << "dat_slices: " << info.integrity_slices;
}

void DumpInode(const Inode* inode, ino_t ino) {
  FX_LOGS(DEBUG) << "inode[" << ino << "]: magic:  " << std::setw(10) << inode->magic;
  FX_LOGS(DEBUG) << "inode[" << ino << "]: size:   " << std::setw(10) << inode->size;
  FX_LOGS(DEBUG) << "inode[" << ino << "]: blocks: " << std::setw(10) << inode->block_count;
  FX_LOGS(DEBUG) << "inode[" << ino << "]: links:  " << std::setw(10) << inode->link_count;
}

void UpdateChecksum(Superblock* info) {
  // Recalculate checksum.
  info->generation_count += 1;
  info->checksum = 0;
  info->checksum = crc32(0, reinterpret_cast<uint8_t*>(info), sizeof(*info));
}

uint32_t CalculateVsliceCount(const Superblock& superblock) {
  // Account for an additional slice for the superblock itself.
  return safemath::checked_cast<uint32_t>(1ull + static_cast<uint64_t>(superblock.ibm_slices) +
                                          static_cast<uint64_t>(superblock.abm_slices) +
                                          static_cast<uint64_t>(superblock.ino_slices) +
                                          static_cast<uint64_t>(superblock.integrity_slices) +
                                          static_cast<uint64_t>(superblock.dat_slices));
}

#ifdef __Fuchsia__
zx::status<> CheckSuperblock(const Superblock& info, block_client::BlockDevice* device,
                             uint32_t max_blocks) {
#else
zx::status<> CheckSuperblock(const Superblock& info, uint32_t max_blocks) {
#endif
  DumpInfo(info);
  if ((info.magic0 != kMinfsMagic0) || (info.magic1 != kMinfsMagic1)) {
    FX_LOGS(ERROR) << "bad magic: " << std::setfill('0') << std::setw(8) << info.magic0
                   << ". Minfs magic: " << std::setfill(' ') << std::setw(8) << kMinfsMagic0;
    return zx::error(ZX_ERR_WRONG_TYPE);
  }
  if (info.major_version != kMinfsCurrentMajorVersion) {
    FX_LOGS(ERROR) << "FS major version: " << std::setfill('0') << std::setw(8) << std::hex
                   << info.major_version << ". Driver major version: " << std::setw(8)
                   << kMinfsCurrentMajorVersion;
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  if ((info.block_size != kMinfsBlockSize) || (info.inode_size != kMinfsInodeSize)) {
    FX_LOGS(ERROR) << "bsz/isz " << info.block_size << "/" << info.inode_size << " unsupported";
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }

  Superblock chksum_info;
  memcpy(&chksum_info, &info, sizeof(chksum_info));
  chksum_info.checksum = 0;
  uint32_t checksum = crc32(0, reinterpret_cast<const uint8_t*>(&chksum_info), sizeof(chksum_info));
  if (info.checksum != checksum) {
    FX_LOGS(ERROR) << "bad checksum: " << info.checksum << ". Expected: " << checksum;
    return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
  }

  TransactionLimits limits(info);
  if ((info.flags & kMinfsFlagFVM) == 0) {
    if (info.dat_block + info.block_count != max_blocks) {
      FX_LOGS(ERROR) << "too large for device";
      return zx::error(ZX_ERR_IO_DATA_INTEGRITY);
    }

    if (info.dat_block - info.integrity_start_block < limits.GetMinimumIntegrityBlocks()) {
      FX_LOGS(ERROR) << "journal too small";
      return zx::error(ZX_ERR_BAD_STATE);
    }
  } else {
    const size_t kBlocksPerSlice = info.slice_size / info.BlockSize();
    zx::status<> status;
#ifdef __Fuchsia__
    status = CheckSlices(info, kBlocksPerSlice, device, /*repair_slices=*/false);
    if (status.is_error()) {
      return status.take_error();
    }
#endif
    status = VerifySlicesSize(info, limits, kBlocksPerSlice);
    if (status.is_error()) {
      return status.take_error();
    }
  }
  return zx::ok();
}

#ifndef __Fuchsia__
BlockOffsets::BlockOffsets(const Bcache& bc, const SuperblockManager& sb) {
  if (bc.extent_lengths_.size() > 0) {
    ZX_ASSERT(bc.extent_lengths_.size() == kExtentCount);
    ibm_block_count_ = static_cast<blk_t>(bc.extent_lengths_[1] / sb.Info().BlockSize());
    abm_block_count_ = static_cast<blk_t>(bc.extent_lengths_[2] / sb.Info().BlockSize());
    ino_block_count_ = static_cast<blk_t>(bc.extent_lengths_[3] / sb.Info().BlockSize());
    integrity_block_count_ = static_cast<blk_t>(bc.extent_lengths_[4] / sb.Info().BlockSize());
    dat_block_count_ = static_cast<blk_t>(bc.extent_lengths_[5] / sb.Info().BlockSize());

    ibm_start_block_ = static_cast<blk_t>(bc.extent_lengths_[0] / sb.Info().BlockSize());
    abm_start_block_ = ibm_start_block_ + ibm_block_count_;
    ino_start_block_ = abm_start_block_ + abm_block_count_;
    integrity_start_block_ = ino_start_block_ + ino_block_count_;
    dat_start_block_ = integrity_start_block_ + integrity_block_count_;
  } else {
    ibm_start_block_ = sb.Info().ibm_block;
    abm_start_block_ = sb.Info().abm_block;
    ino_start_block_ = sb.Info().ino_block;
    integrity_start_block_ = sb.Info().integrity_start_block;
    dat_start_block_ = sb.Info().dat_block;

    ibm_block_count_ = abm_start_block_ - ibm_start_block_;
    abm_block_count_ = ino_start_block_ - abm_start_block_;
    ino_block_count_ = dat_start_block_ - ino_start_block_;
    integrity_block_count_ = dat_start_block_ - integrity_start_block_;
    dat_block_count_ = sb.Info().block_count;
  }
}
#endif

std::unique_ptr<Bcache> Minfs::Destroy(std::unique_ptr<Minfs> minfs) {
#ifdef __Fuchsia__
  minfs->StopWriteback();
#endif
  return std::move(minfs->bc_);
}

zx::status<std::unique_ptr<Transaction>> Minfs::BeginTransaction(size_t reserve_inodes,
                                                                 size_t reserve_blocks) {
  ZX_DEBUG_ASSERT(reserve_inodes <= TransactionLimits::kMaxInodeBitmapBlocks);
#ifdef __Fuchsia__
  if (journal_ == nullptr) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  if (!journal_->IsWritebackEnabled()) {
    return zx::error(ZX_ERR_IO_REFUSED);
  }

  // TODO(planders): Once we are splitting up write transactions, assert this on host as well.
  ZX_DEBUG_ASSERT(reserve_blocks <= limits_.GetMaximumDataBlocks());
#endif

  // Reserve blocks from allocators before returning WritebackWork to client.
  auto transaction_or = Transaction::Create(this, reserve_inodes, reserve_blocks, inodes_.get());
#ifdef __Fuchsia__

  if (transaction_or.status_value() == ZX_ERR_NO_SPACE &&
      (reserve_blocks > 0 || reserve_inodes > 0)) {
    // When there's no more space, flush the journal in case a recent transaction has freed blocks
    // but has yet to be flushed from the journal and committed. Then, try again.
    FX_LOGS(INFO)
        << "Unable to reserve blocks. Flushing journal in attempt to reclaim unlinked blocks.";

    auto sync_status = BlockingJournalSync();
    if (sync_status.is_error()) {
      FX_LOGS(ERROR) << "Failed to flush journal (status: " << sync_status.status_string() << ")";
      inspect_tree_.OnOutOfSpace();
      // Return the original status.
      return transaction_or.take_error();
    }

    transaction_or = Transaction::Create(this, reserve_inodes, reserve_blocks, inodes_.get());
    if (transaction_or.is_ok()) {
      inspect_tree_.OnRecoveredSpace();
    }
  }

  if (transaction_or.is_error()) {
    FX_LOGS(ERROR) << "Failed to reserve blocks for transaction (status: "
                   << transaction_or.status_string() << ")";
    if (transaction_or.error_value() == ZX_ERR_NO_SPACE) {
      inspect_tree_.OnOutOfSpace();
    }
  }
#endif

  return transaction_or;
}

#ifdef __Fuchsia__
void Minfs::EnqueueCallback(SyncCallback callback) {
  if (callback) {
    journal_->schedule_task(journal_->Sync().then(
        [closure = std::move(callback)](fpromise::result<void, zx_status_t>& result) mutable
        -> fpromise::result<void, zx_status_t> {
          if (result.is_ok()) {
            closure(ZX_OK);
          } else {
            closure(result.error());
          }
          return fpromise::ok();
        }));
  } else {
    journal_->schedule_task(journal_->Sync());
  }
}
#endif

// To be used with promises to hold on to an object and release it when executed. It is used below
// to pin vnodes that might be referenced in a transaction and to keep deallocated blocks reserved
// until the transaction hits the device. See below for more.
template <typename T>
class ReleaseObject {
 public:
  explicit ReleaseObject(T object) : object_(std::move(object)) {}

  void operator()([[maybe_unused]] const zx::status<void>& dont_care) { object_.reset(); }

 private:
  std::optional<T> object_;
};

void Minfs::CommitTransaction(std::unique_ptr<Transaction> transaction) {
  transaction->inode_reservation().Commit(transaction.get());
  transaction->block_reservation().Commit(transaction.get());
  if (sb_->is_dirty()) {
    sb_->Write(transaction.get(), UpdateBackupSuperblock::kNoUpdate);
  }

#ifdef __Fuchsia__
  ZX_DEBUG_ASSERT(journal_ != nullptr);

  auto data_operations = transaction->RemoveDataOperations();
  auto metadata_operations = transaction->RemoveMetadataOperations();
  ZX_DEBUG_ASSERT(BlockCount(metadata_operations) <= limits_.GetMaximumEntryDataBlocks());

  TRACE_DURATION("minfs", "CommitTransaction", "data_ops", data_operations.size(), "metadata_ops",
                 metadata_operations.size());

  // We take the pending block deallocations here and hold on to them until the transaction has
  // committed. Otherwise, it would be possible for data writes in a later transaction to make it
  // out to those blocks, but if the transaction that freed those blocks doesn't make it, we will
  // have erroneously overwritten those blocks. We don't need to do the same for inode allocations
  // because writes to those blocks are always done via the journal which are always sequenced.
  //
  // There are some potential optimisations that probably aren't worth doing:
  //
  //  * We only need to keep the blocks reserved for data writes. We could allow the blocks to be
  //    used for metadata (e.g. indirect blocks).
  //
  //  * The allocator will currently reserve inodes that are freed in the same transaction i.e. it
  //    won't be possible to use free inodes until the next transaction. This probably can't
  //    happen anyway.
  zx_status_t status = journal_->CommitTransaction(
      {.metadata_operations = metadata_operations,
       .data_promise = data_operations.empty() ? fs::Journal::Promise()
                                               : journal_->WriteData(std::move(data_operations)),
       // Keep blocks reserved until committed.
       .commit_callback = [pending_deallocations =
                               transaction->block_reservation().TakePendingDeallocations()] {},
       // Keep vnodes alive until complete because we cache data and it's not safe to read new
       // data until the transaction is complete (and we could end up doing that if the vnode
       // gets destroyed and then quickly recreated).
       .complete_callback = [pinned_vnodes = transaction->RemovePinnedVnodes()] {}});
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "CommitTransaction failed: " << zx_status_get_string(status);
  }

  // Update filesystem usage information now that the transaction has been committed.
  inspect_tree_.UpdateSpaceUsage(Info(), BlocksReserved());

  if (!journal_sync_task_.is_pending()) {
    // During mount, there isn't a dispatcher, so we won't queue a flush, but that won't matter
    // since the only changes will be things like whether the volume is clean and it doesn't
    // matter if they're not persisted.
    async_dispatcher_t* d = dispatcher();
    if (d) {
      journal_sync_task_.PostDelayed(d, kJournalBackgroundSyncTime);
    }
  }
#else
  bc_->RunRequests(transaction->TakeOperations());
#endif
}

void Minfs::FsckAtEndOfTransaction() {
#ifdef __Fuchsia__
  bc_->Pause();
  {
    auto bcache_or = Bcache::Create(bc_->device(), bc_->Maxblk());
    ZX_ASSERT(bcache_or.is_ok());
    bcache_or = Fsck(std::move(bcache_or.value()), FsckOptions{.read_only = true, .quiet = true});
    ZX_ASSERT(bcache_or.is_ok());
  }
  bc_->Resume();
#endif
}

#ifdef __Fuchsia__
void Minfs::Sync(SyncCallback closure) {
  if (journal_ == nullptr) {
    if (closure)
      closure(ZX_OK);
    return;
  }
  auto dirty_vnodes = GetDirtyVnodes();
  for (auto vnode : dirty_vnodes) {
    auto status = vnode->FlushCachedWrites();
    ZX_ASSERT(status.is_ok());
  }
  EnqueueCallback(std::move(closure));
}
#endif

#ifdef __Fuchsia__
Minfs::Minfs(async_dispatcher_t* dispatcher, std::unique_ptr<Bcache> bc,
             std::unique_ptr<SuperblockManager> sb, std::unique_ptr<Allocator> block_allocator,
             std::unique_ptr<InodeManager> inodes, const MountOptions& mount_options)
    : fs::ManagedVfs(dispatcher),
      bc_(std::move(bc)),
      sb_(std::move(sb)),
      block_allocator_(std::move(block_allocator)),
      inodes_(std::move(inodes)),
      journal_sync_task_([this]() { Sync(); }),
      inspect_tree_(bc_->device()),
      limits_(sb_->Info()),
      mount_options_(mount_options) {
  zx::event::create(0, &fs_id_);
}
#else
Minfs::Minfs(std::unique_ptr<Bcache> bc, std::unique_ptr<SuperblockManager> sb,
             std::unique_ptr<Allocator> block_allocator, std::unique_ptr<InodeManager> inodes,
             BlockOffsets offsets, const MountOptions& mount_options)
    : bc_(std::move(bc)),
      sb_(std::move(sb)),
      block_allocator_(std::move(block_allocator)),
      inodes_(std::move(inodes)),
      offsets_(offsets),
      limits_(sb_->Info()),
      mount_options_(mount_options) {}
#endif

Minfs::~Minfs() { vnode_hash_.clear(); }

zx::status<> Minfs::InoFree(Transaction* transaction, VnodeMinfs* vn) {
  TRACE_DURATION("minfs", "Minfs::InoFree", "ino", vn->GetIno());

#ifdef __Fuchsia__
  vn->CancelPendingWriteback();
#endif

  inodes_->Free(transaction, vn->GetIno());

  auto status = vn->BlocksShrink(transaction, 0);
  if (status.is_error())
    return status;
  vn->MarkPurged();
  InodeUpdate(transaction, vn->GetIno(), vn->GetInode());

  ZX_DEBUG_ASSERT(vn->GetInode()->block_count == 0);
  ZX_DEBUG_ASSERT(vn->IsUnlinked());
  return zx::ok();
}

void Minfs::AddUnlinked(PendingWork* transaction, VnodeMinfs* vn) {
  ZX_DEBUG_ASSERT(vn->GetInode()->link_count == 0);

  Superblock* info = sb_->MutableInfo();

  if (info->unlinked_tail == 0) {
    // If no other vnodes are unlinked, |vn| is now both the head and the tail.
    ZX_DEBUG_ASSERT(info->unlinked_head == 0);
    info->unlinked_head = vn->GetIno();
    info->unlinked_tail = vn->GetIno();
  } else {
    // Since all vnodes in the unlinked list are necessarily open, the last vnode
    // must currently exist in the vnode lookup.
    fbl::RefPtr<VnodeMinfs> last_vn = VnodeLookupInternal(info->unlinked_tail);
    ZX_DEBUG_ASSERT(last_vn != nullptr);

    // Add |vn| to the end of the unlinked list.
    last_vn->SetNextInode(vn->GetIno());
    vn->SetLastInode(last_vn->GetIno());
    info->unlinked_tail = vn->GetIno();

    last_vn->InodeSync(transaction, kMxFsSyncDefault);
    vn->InodeSync(transaction, kMxFsSyncDefault);
  }
}

void Minfs::RemoveUnlinked(PendingWork* transaction, VnodeMinfs* vn) {
  if (vn->GetInode()->last_inode == 0) {
    // If |vn| is the first unlinked inode, we just need to update the list head
    // to the next inode (which may not exist).
    ZX_DEBUG_ASSERT_MSG(Info().unlinked_head == vn->GetIno(),
                        "Vnode %u has no previous link, but is not listed as unlinked list head",
                        vn->GetIno());
    sb_->MutableInfo()->unlinked_head = vn->GetInode()->next_inode;
  } else {
    // Set the previous vnode's next to |vn|'s next.
    fbl::RefPtr<VnodeMinfs> last_vn = VnodeLookupInternal(vn->GetInode()->last_inode);
    ZX_DEBUG_ASSERT(last_vn != nullptr);
    last_vn->SetNextInode(vn->GetInode()->next_inode);
    last_vn->InodeSync(transaction, kMxFsSyncDefault);
  }

  if (vn->GetInode()->next_inode == 0) {
    // If |vn| is the last unlinked inode, we just need to update the list tail
    // to the previous inode (which may not exist).
    ZX_DEBUG_ASSERT_MSG(Info().unlinked_tail == vn->GetIno(),
                        "Vnode %u has no next link, but is not listed as unlinked list tail",
                        vn->GetIno());
    sb_->MutableInfo()->unlinked_tail = vn->GetInode()->last_inode;
  } else {
    // Set the next vnode's previous to |vn|'s previous.
    fbl::RefPtr<VnodeMinfs> next_vn = VnodeLookupInternal(vn->GetInode()->next_inode);
    ZX_DEBUG_ASSERT(next_vn != nullptr);
    next_vn->SetLastInode(vn->GetInode()->last_inode);
    next_vn->InodeSync(transaction, kMxFsSyncDefault);
  }
}

zx::status<> Minfs::PurgeUnlinked() {
  ino_t last_ino = 0;
  ino_t next_ino = Info().unlinked_head;
  ino_t unlinked_count = 0;

  if (next_ino == 0) {
    ZX_DEBUG_ASSERT(Info().unlinked_tail == 0);
    return zx::ok();
  }

  // Loop through the unlinked list and free all allocated resources.
  fbl::RefPtr<VnodeMinfs> vn;
  VnodeMinfs::Recreate(this, next_ino, &vn);
  ZX_DEBUG_ASSERT(vn->GetInode()->last_inode == 0);

  do {
    auto transaction_or = BeginTransaction(0, 0);
    if (transaction_or.is_error()) {
      return transaction_or.take_error();
    }

    ZX_DEBUG_ASSERT(vn->GetInode()->link_count == 0);

    if (auto status = InoFree(transaction_or.value().get(), vn.get()); status.is_error()) {
      return status;
    }

    last_ino = next_ino;
    next_ino = vn->GetInode()->next_inode;

    sb_->MutableInfo()->unlinked_head = next_ino;

    if (next_ino == 0) {
      ZX_DEBUG_ASSERT(Info().unlinked_tail == last_ino);
      sb_->MutableInfo()->unlinked_tail = 0;
    } else {
      // Fix the last_inode pointer in the next inode.
      VnodeMinfs::Recreate(this, next_ino, &vn);
      ZX_DEBUG_ASSERT(vn->GetInode()->last_inode == last_ino);
      vn->GetMutableInode()->last_inode = 0;
      InodeUpdate(transaction_or.value().get(), next_ino, vn->GetInode());
    }
    CommitTransaction(std::move(transaction_or.value()));
    unlinked_count++;
  } while (next_ino != 0);

  ZX_DEBUG_ASSERT(Info().unlinked_head == 0);
  ZX_DEBUG_ASSERT(Info().unlinked_tail == 0);

  if (!mount_options_.quiet) {
    FX_LOGS(WARNING) << "Found and purged " << unlinked_count << " unlinked vnode(s) on mount";
  }

  return zx::ok();
}

#ifdef __Fuchsia__
zx::status<> Minfs::UpdateCleanBitAndOldestRevision(bool is_clean) {
  auto transaction_or = BeginTransaction(0, 0);
  if (transaction_or.is_error()) {
    FX_LOGS(ERROR) << "failed to " << (is_clean ? "set" : "unset")
                   << " clean flag: " << transaction_or.error_value();
    return transaction_or.take_error();
  }
  if (kMinfsCurrentMinorVersion < Info().oldest_minor_version) {
    sb_->MutableInfo()->oldest_minor_version = kMinfsCurrentMinorVersion;
  }
  UpdateFlags(transaction_or.value().get(), kMinfsFlagClean, is_clean);
  CommitTransaction(std::move(transaction_or.value()));
  // Mount/unmount marks filesystem as dirty/clean. When we called UpdateFlags
  // above, the underlying subsystems may complete the IO asynchronously. But
  // these operations(and any other operations issued before) should be
  // persisted to final location before we allow any other operation to the
  // filesystem or before we return completion status to the caller.
  return BlockingJournalSync();
}

void Minfs::StopWriteback() {
  // Minfs already terminated.
  if (!bc_) {
    return;
  }

  if (IsReadonly() == false) {
    // Ignore errors here since there is nothing we can do.
    [[maybe_unused]] auto _ = UpdateCleanBitAndOldestRevision(/*is_clean=*/true);
  }

  journal_ = nullptr;
  [[maybe_unused]] auto _ = bc_->Sync();
}
#endif

fbl::RefPtr<VnodeMinfs> Minfs::VnodeLookupInternal(uint32_t ino) {
#ifdef __Fuchsia__
  fbl::RefPtr<VnodeMinfs> vn;
  {
    // Avoid releasing a reference to |vn| while holding |hash_lock_|.
    fbl::AutoLock lock(&hash_lock_);
    auto rawVn = vnode_hash_.find(ino);
    if (!rawVn.IsValid()) {
      // Nothing exists in the lookup table
      return nullptr;
    }
    vn = fbl::MakeRefPtrUpgradeFromRaw(rawVn.CopyPointer(), hash_lock_);
    if (vn == nullptr) {
      // The vn 'exists' in the map, but it is being deleted.
      // Remove it (by key) so the next person doesn't trip on it,
      // and so we can insert another node with the same key into the hash
      // map.
      // Notably, VnodeRelease erases the vnode by object, not key,
      // so it will not attempt to replace any distinct Vnodes that happen
      // to be re-using the same inode.
      vnode_hash_.erase(ino);
    }
  }
  return vn;
#else
  return fbl::RefPtr(vnode_hash_.find(ino).CopyPointer());
#endif
}

void Minfs::InoNew(Transaction* transaction, const Inode* inode, ino_t* out_ino) {
  size_t allocated_ino = transaction->AllocateInode();
  *out_ino = static_cast<ino_t>(allocated_ino);
  // Write the inode back to storage.
  InodeUpdate(transaction, *out_ino, inode);
}

zx::status<fbl::RefPtr<VnodeMinfs>> Minfs::VnodeNew(Transaction* transaction, uint32_t type) {
  TRACE_DURATION("minfs", "Minfs::VnodeNew");
  if ((type != kMinfsTypeFile) && (type != kMinfsTypeDir)) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fbl::RefPtr<VnodeMinfs> vn;

  // Allocate the in-memory vnode
  VnodeMinfs::Allocate(this, type, &vn);

  // Allocate the on-disk inode
  ino_t ino;
  InoNew(transaction, vn->GetInode(), &ino);
  vn->SetIno(ino);
  VnodeInsert(vn.get());

  return zx::ok(std::move(vn));
}

void Minfs::VnodeInsert(VnodeMinfs* vn) {
#ifdef __Fuchsia__
  fbl::AutoLock lock(&hash_lock_);
#endif

  ZX_DEBUG_ASSERT_MSG(!vnode_hash_.find(vn->GetKey()).IsValid(), "ino %u already in map\n",
                      vn->GetKey());
  vnode_hash_.insert(vn);
}

fbl::RefPtr<VnodeMinfs> Minfs::VnodeLookup(uint32_t ino) {
  fbl::RefPtr<VnodeMinfs> vn = VnodeLookupInternal(ino);
#ifdef __Fuchsia__
  if (vn != nullptr && vn->IsUnlinked()) {
    vn = nullptr;
  }
#endif
  return vn;
}

void Minfs::VnodeRelease(VnodeMinfs* vn) {
#ifdef __Fuchsia__
  fbl::AutoLock lock(&hash_lock_);
#endif
  vnode_hash_.erase(*vn);
}

zx::status<fbl::RefPtr<VnodeMinfs>> Minfs::VnodeGet(ino_t ino) {
  TRACE_DURATION("minfs", "Minfs::VnodeGet", "ino", ino);
  if ((ino < 1) || (ino >= Info().inode_count)) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  fbl::RefPtr<VnodeMinfs> vn = VnodeLookup(ino);
  if (vn != nullptr) {
    return zx::ok(std::move(vn));
  }

  VnodeMinfs::Recreate(this, ino, &vn);

  if (vn->IsUnlinked()) {
    // If a vnode we have recreated from disk is unlinked, something has gone wrong during the
    // unlink process and our filesystem is now in an inconsistent state. In order to avoid
    // further inconsistencies, prohibit access to this vnode.
    FX_LOGS(WARNING) << "Attempted to load unlinked vnode " << ino;
    return zx::error(ZX_ERR_BAD_STATE);
  }

  VnodeInsert(vn.get());
  return zx::ok(std::move(vn));
}

// Allocate a new data block from the block bitmap.
void Minfs::BlockNew(PendingWork* transaction, blk_t* out_bno) const {
  size_t allocated_bno = transaction->AllocateBlock();
  *out_bno = static_cast<blk_t>(allocated_bno);
  ValidateBno(*out_bno);
}

bool Minfs::IsReadonly() {
  std::lock_guard lock(vfs_lock_);
  return ReadonlyLocked();
}

void Minfs::UpdateFlags(PendingWork* transaction, uint32_t flags, bool set) {
  if (set) {
    sb_->MutableInfo()->flags |= flags;
  } else {
    sb_->MutableInfo()->flags &= (~flags);
  }
  sb_->Write(transaction, UpdateBackupSuperblock::kUpdate);
}

#ifdef __Fuchsia__
void Minfs::BlockSwap(Transaction* transaction, blk_t in_bno, blk_t* out_bno) {
  if (in_bno > 0) {
    ValidateBno(in_bno);
  }

  size_t allocated_bno = transaction->SwapBlock(in_bno);
  *out_bno = static_cast<blk_t>(allocated_bno);
  ValidateBno(*out_bno);
}
#endif

void InitializeDirectory(void* bdata, ino_t ino_self, ino_t ino_parent) {
  // The self directory is named "." (name length = 1).
  constexpr auto kSelfSize = DirentSize(1);
  DirentBuffer self;
  self.dirent.ino = ino_self;
  self.dirent.reclen = kSelfSize;
  self.dirent.namelen = 1;
  self.dirent.type = kMinfsTypeDir;
  self.dirent.name[0] = '.';

  // The parent directory is named ".." (name length = 2).
  constexpr auto kParentSize = DirentSize(2);
  DirentBuffer parent;
  parent.dirent.ino = ino_parent;
  parent.dirent.reclen = kParentSize | kMinfsReclenLast;
  parent.dirent.namelen = 2;
  parent.dirent.type = kMinfsTypeDir;
  parent.dirent.name[0] = '.';
  parent.dirent.name[1] = '.';

  // Construct the output buffer by appending the two entries.
  memcpy(bdata, self.raw, kSelfSize);
  memcpy(&static_cast<uint8_t*>(bdata)[kSelfSize], parent.raw, kParentSize);
}

zx::status<std::pair<std::unique_ptr<Allocator>, std::unique_ptr<InodeManager>>>
Minfs::ReadInitialBlocks(const Superblock& info, Bcache& bc, SuperblockManager& superblock,
                         const MountOptions& mount_options) {
#ifdef __Fuchsia__
  const blk_t abm_start_block = superblock.Info().abm_block;
  const blk_t ibm_start_block = superblock.Info().ibm_block;
  const blk_t ino_start_block = superblock.Info().ino_block;
#else
  BlockOffsets offsets(bc, superblock);
  const blk_t abm_start_block = offsets.AbmStartBlock();
  const blk_t ibm_start_block = offsets.IbmStartBlock();
  const blk_t ino_start_block = offsets.InoStartBlock();
#endif

  fs::BufferedOperationsBuilder builder;

  // Block Bitmap allocator initialization.
  AllocatorFvmMetadata block_allocator_fvm =
      AllocatorFvmMetadata(&superblock, SuperblockAllocatorAccess::Blocks());
  AllocatorMetadata block_allocator_meta = AllocatorMetadata(
      info.dat_block, abm_start_block, (info.flags & kMinfsFlagFVM) != 0,
      std::move(block_allocator_fvm), &superblock, SuperblockAllocatorAccess::Blocks());

  std::unique_ptr<PersistentStorage> storage(
#ifdef __Fuchsia__
      new PersistentStorage(bc.device(), &superblock, superblock.Info().BlockSize(), nullptr,
                            std::move(block_allocator_meta), superblock.BlockSize()));
#else
      new PersistentStorage(&superblock, superblock.Info().BlockSize(), nullptr,
                            std::move(block_allocator_meta), superblock.BlockSize()));
#endif

  auto block_allocator_or = Allocator::Create(&builder, std::move(storage));
  if (block_allocator_or.is_error()) {
    FX_LOGS(ERROR) << "Create failed to initialize block allocator: "
                   << block_allocator_or.error_value();
    return block_allocator_or.take_error();
  }

  // Inode Bitmap allocator initialization.
  AllocatorFvmMetadata inode_allocator_fvm =
      AllocatorFvmMetadata(&superblock, SuperblockAllocatorAccess::Inodes());
  AllocatorMetadata inode_allocator_meta = AllocatorMetadata(
      ino_start_block, ibm_start_block, (info.flags & kMinfsFlagFVM) != 0,
      std::move(inode_allocator_fvm), &superblock, SuperblockAllocatorAccess::Inodes());

#ifdef __Fuchsia__
  auto inodes_or =
      InodeManager::Create(bc.device(), &superblock, &builder, std::move(inode_allocator_meta),
                           ino_start_block, info.inode_count);
#else
  auto inodes_or = InodeManager::Create(&bc, &superblock, &builder, std::move(inode_allocator_meta),
                                        ino_start_block, info.inode_count);
#endif
  if (inodes_or.is_error()) {
    FX_LOGS(ERROR) << "Create failed to initialize inodes: " << inodes_or.error_value();
    return inodes_or.take_error();
  }

  zx_status_t status = bc.RunRequests(builder.TakeOperations());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Create failed to read initial blocks: " << status;
    return zx::error(status);
  }

  return zx::ok(
      std::make_pair(std::move(block_allocator_or.value()), std::move(inodes_or.value())));
}

zx::status<std::unique_ptr<Minfs>> Minfs::Create(FuchsiaDispatcher* dispatcher,
                                                 std::unique_ptr<Bcache> bc,
                                                 const MountOptions& options) {
  // Read the superblock before replaying the journal.
  auto info_or = LoadSuperblockWithRepair(bc.get(), options.repair_filesystem);
  if (info_or.is_error()) {
    return info_or.take_error();
  }

  Superblock& info = info_or.value();

#ifdef __Fuchsia__
  if ((info.flags & kMinfsFlagClean) == 0 && !options.quiet) {
    FX_LOGS(WARNING) << "filesystem not unmounted cleanly.";
  }

  // Replay the journal before loading any other structures.
  zx::status<fs::JournalSuperblock> journal_superblock_or;
  if (!options.readonly) {
    journal_superblock_or = ReplayJournalReloadSuperblock(bc.get(), &info);
    if (journal_superblock_or.is_error()) {
      return journal_superblock_or.take_error();
    }
  } else if (!options.quiet) {
    FX_LOGS(WARNING) << "Not replaying journal";
  }
#endif

#ifndef __Fuchsia__
  if (bc->extent_lengths_.size() != 0 && bc->extent_lengths_.size() != kExtentCount) {
    FX_LOGS(ERROR) << "invalid number of extents";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
#endif

  IntegrityCheck checks = options.repair_filesystem ? IntegrityCheck::kAll : IntegrityCheck::kNone;
  zx::status<std::unique_ptr<SuperblockManager>> sb_or;
#ifdef __Fuchsia__
  block_client::BlockDevice* device = bc->device();
  sb_or = SuperblockManager::Create(device, info, bc->Maxblk(), checks);
#else
  sb_or = SuperblockManager::Create(info, bc->Maxblk(), checks);
#endif

  if (sb_or.is_error()) {
    FX_LOGS(ERROR) << "Create failed to initialize superblock: " << sb_or.error_value();
    return sb_or.take_error();
  }

  std::unique_ptr<SuperblockManager> sb = std::move(sb_or.value());

  auto result = Minfs::ReadInitialBlocks(info, *bc, *sb, options);
  if (result.is_error())
    return result.take_error();
  auto [block_allocator, inodes] = std::move(result).value();

  std::unique_ptr<Minfs> out_fs;

#ifdef __Fuchsia__
  out_fs =
      std::unique_ptr<Minfs>(new Minfs(dispatcher, std::move(bc), std::move(sb),
                                       std::move(block_allocator), std::move(inodes), options));
  if (!options.readonly) {
    auto status = out_fs->InitializeJournal(std::move(journal_superblock_or.value()));
    if (status.is_error()) {
      FX_LOGS(ERROR) << "Cannot initialize journal";
      return status.take_error();
    }

    if (options.fsck_after_every_transaction) {
      FX_LOGS(ERROR) << "Will fsck after every transaction";
      out_fs->journal_->set_write_metadata_callback(
          fit::bind_member<&Minfs::FsckAtEndOfTransaction>(out_fs.get()));
    }
  }

  if (options.repair_filesystem && (info.flags & kMinfsFlagFVM)) {
    // After replaying the journal, it's now safe to repair the FVM slices.
    const size_t kBlocksPerSlice = info.slice_size / info.BlockSize();
    auto status = CheckSlices(info, kBlocksPerSlice, device, /*repair_slices=*/true);
    if (status.is_error()) {
      return status.take_error();
    }
  }

  if (!options.readonly) {
    // On a read-write filesystem we unset the kMinfsFlagClean flag to indicate that the
    // filesystem may begin receiving modifications.
    //
    // The kMinfsFlagClean flag is reset on orderly shutdown.
    auto status = out_fs->UpdateCleanBitAndOldestRevision(/*is_clean=*/false);
    if (status.is_error()) {
      return status.take_error();
    }

    // After loading the rest of the filesystem, purge any remaining nodes in the unlinked list.
    status = out_fs->PurgeUnlinked();
    if (status.is_error()) {
      FX_LOGS(ERROR) << "Cannot purge unlinked list";
      return status.take_error();
    }

    if (options.readonly_after_initialization) {
      // The filesystem should still be "writable"; we set the dirty bit while
      // purging the unlinked list. Invoking StopWriteback here unsets the dirty bit.
      out_fs->StopWriteback();
    }
  }

  out_fs->SetReadonly(options.readonly || options.readonly_after_initialization);

  out_fs->mount_state_ = {
      .readonly_after_initialization = options.readonly_after_initialization,
      .verbose = options.verbose,
      .repair_filesystem = options.repair_filesystem,
      .use_journal = true,
      .dirty_cache_enabled = true,
  };

  out_fs->InitializeInspectTree();
#else
  BlockOffsets offsets(*bc, *sb);
  out_fs =
      std::unique_ptr<Minfs>(new Minfs(std::move(bc), std::move(sb), std::move(block_allocator),
                                       std::move(inodes), offsets, options));
#endif  // !defined(__Fuchsia__)

  return zx::ok(std::move(out_fs));
}  // namespace minfs

#ifdef __Fuchsia__
zx::status<fs::JournalSuperblock> ReplayJournal(Bcache* bc, const Superblock& info) {
  FX_LOGS(INFO) << "Replaying journal";

  auto superblock_or =
      fs::ReplayJournal(bc, bc, JournalStartBlock(info), JournalBlocks(info), info.BlockSize());
  if (superblock_or.is_error()) {
    FX_LOGS(ERROR) << "Failed to replay journal";
  } else {
    FX_LOGS(DEBUG) << "Journal replayed";
  }

  return superblock_or;
}

zx::status<> Minfs::InitializeJournal(fs::JournalSuperblock journal_superblock) {
  if (journal_ != nullptr) {
    FX_LOGS(ERROR) << "Journal was already initialized.";
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }

  const uint64_t journal_entry_blocks = JournalBlocks(sb_->Info()) - fs::kJournalMetadataBlocks;
  std::unique_ptr<storage::BlockingRingBuffer> journal_buffer;
  zx_status_t status = storage::BlockingRingBuffer::Create(GetMutableBcache(), journal_entry_blocks,
                                                           sb_->Info().BlockSize(),
                                                           "minfs-journal-buffer", &journal_buffer);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot create journal buffer";
    return zx::error(status);
  }

  std::unique_ptr<storage::BlockingRingBuffer> writeback_buffer;
  status = storage::BlockingRingBuffer::Create(GetMutableBcache(), WritebackCapacity(),
                                               sb_->Info().BlockSize(), "minfs-writeback-buffer",
                                               &writeback_buffer);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot create writeback buffer";
    return zx::error(status);
  }

  journal_ = std::make_unique<fs::Journal>(GetMutableBcache(), std::move(journal_superblock),
                                           std::move(journal_buffer), std::move(writeback_buffer),
                                           JournalStartBlock(sb_->Info()), fs::Journal::Options());
  return zx::ok();
}

void Minfs::InitializeInspectTree() {
  zx::status<fs::FilesystemInfo> fs_info{GetFilesystemInfo()};
  if (fs_info.is_error()) {
    FX_LOGS(ERROR) << "Failed to initialize Minfs inspect tree: GetFilesystemInfo returned "
                   << fs_info.status_string();
    return;
  }
  inspect_tree_.Initialize(fs_info.value(), Info(), BlocksReserved());
}

#endif

zx::status<std::unique_ptr<Minfs>> Mount(FuchsiaDispatcher* dispatcher,
                                         std::unique_ptr<minfs::Bcache> bc,
                                         const MountOptions& options,
                                         fbl::RefPtr<VnodeMinfs>* root_out) {
  TRACE_DURATION("minfs", "minfs_mount");

  auto fs_or = Minfs::Create(dispatcher, std::move(bc), options);
  if (fs_or.is_error()) {
    FX_LOGS(ERROR) << "failed to create filesystem object " << fs_or.error_value();
    return fs_or.take_error();
  }

  auto vn_or = fs_or->VnodeGet(kMinfsRootIno);
  if (vn_or.is_error()) {
    FX_LOGS(ERROR) << "cannot find root inode: " << vn_or.is_error();
    return vn_or.take_error();
  }

  ZX_DEBUG_ASSERT(vn_or->IsDirectory());

  *root_out = std::move(vn_or.value());
  return zx::ok(std::move(fs_or.value()));
}

#ifdef __Fuchsia__
void Minfs::LogMountMetrics() {
  if (!mount_options_.cobalt_factory) {
    cobalt_logger_ = cobalt::NewCobaltLoggerFromProjectId(
        dispatcher(), sys::ServiceDirectory::CreateFromNamespace(), fs_metrics::kCobaltProjectId);
  } else {
    cobalt_logger_ = mount_options_.cobalt_factory();
  }
  cobalt_logger_->LogEventCount(
      static_cast<uint32_t>(fs_metrics::Event::kVersion),
      static_cast<uint32_t>(fs_metrics::Source::kMinfs),
      std::to_string(Info().major_version) + "/" + std::to_string(Info().oldest_minor_version), {},
      1);
}

void Minfs::Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) {
  // On a read-write filesystem, set the kMinfsFlagClean on a clean unmount.
  FX_LOGS(INFO) << "Shutting down";
  ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Managed VFS shutdown failed with status: " << zx_status_get_string(status);
    }
    Sync([this, cb = std::move(cb)](zx_status_t sync_status) mutable {
      if (sync_status != ZX_OK) {
        FX_LOGS(ERROR) << "Sync at unmount failed with status: "
                       << zx_status_get_string(sync_status);
      }
      async::PostTask(dispatcher(), [this, cb = std::move(cb)]() mutable {
        // Ensure writeback buffer completes before auxiliary structures are deleted.
        StopWriteback();

        auto on_unmount = std::move(on_unmount_);

        // Shut down the block cache.
        bc_.reset();

        // TODO(/fxbug.dev/90054): Report sync and managed shutdown status.
        // Identify to the unmounting channel that teardown is complete.
        cb(ZX_OK);

        // Identify to the unmounting thread that teardown is complete.
        if (on_unmount) {
          on_unmount();
        }
      });
    });
  });
}

zx::status<fs::FilesystemInfo> Minfs::GetFilesystemInfo() {
  fs::FilesystemInfo info;

  info.SetFsId(fs_id_);
  info.name = "minfs";
  info.fs_type = VFS_TYPE_MINFS;

  info.block_size = static_cast<uint32_t>(BlockSize());
  info.max_filename_size = kMinfsMaxNameSize;

  fs_inspect::UsageData usage = CalculateSpaceUsage(Info(), BlocksReserved());
  info.total_bytes = usage.total_bytes;
  info.used_bytes = usage.used_bytes;
  info.total_nodes = usage.total_nodes;
  info.used_nodes = usage.used_nodes;

  const block_client::BlockDevice* device = bc_->device();
  if (device) {
    zx::status<fs_inspect::VolumeData::SizeInfo> size_info =
        fs_inspect::VolumeData::GetSizeInfoFromDevice(*device);
    if (size_info.is_ok()) {
      info.free_shared_pool_bytes = size_info->available_space_bytes;
    } else {
      FX_LOGS(DEBUG) << "Unable to obtain available space: " << size_info.status_string();
    }
  }

  return zx::ok(info);
}

void Minfs::OnNoConnections() {
  if (IsTerminating()) {
    return;
  }
  Shutdown([](zx_status_t status) mutable {
    ZX_ASSERT_MSG(status == ZX_OK, "Filesystem shutdown failed on OnNoConnections(): %s",
                  zx_status_get_string(status));
  });
}
#endif

uint32_t BlocksRequiredForInode(uint64_t inode_count) {
  return safemath::checked_cast<uint32_t>((inode_count + kMinfsInodesPerBlock - 1) /
                                          kMinfsInodesPerBlock);
}

uint32_t BlocksRequiredForBits(uint64_t bit_count) {
  return safemath::checked_cast<uint32_t>((bit_count + kMinfsBlockBits - 1) / kMinfsBlockBits);
}

zx::status<> Mkfs(const MountOptions& options, Bcache* bc) {
  Superblock info;
  memset(&info, 0x00, sizeof(info));
  info.magic0 = kMinfsMagic0;
  info.magic1 = kMinfsMagic1;
  info.major_version = kMinfsCurrentMajorVersion;
  info.flags = kMinfsFlagClean;
  info.block_size = kMinfsBlockSize;
  info.inode_size = kMinfsInodeSize;

  uint32_t blocks = 0;
  uint32_t inodes = 0;

#ifdef __Fuchsia__
  auto fvm_cleanup = fit::defer([device = bc->device(), &info]() { FreeSlices(&info, device); });
  if (auto status = CreateFvmData(options, &info, bc->device()); status.is_error()) {
    return status.take_error();
  }

  inodes = static_cast<uint32_t>(info.ino_slices * info.slice_size / kMinfsInodeSize);
  blocks = static_cast<uint32_t>(info.dat_slices * info.slice_size / info.BlockSize());
#endif
  if ((info.flags & kMinfsFlagFVM) == 0) {
    inodes = kMinfsDefaultInodeCount;
    blocks = bc->Maxblk();
  }

  // Determine how many blocks of inodes, allocation bitmaps,
  // and inode bitmaps there are
  uint32_t inoblks = (inodes + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
  uint32_t ibmblks = (inodes + kMinfsBlockBits - 1) / kMinfsBlockBits;
  uint32_t abmblks = 0;

  info.inode_count = inodes;
  info.alloc_block_count = 0;
  info.alloc_inode_count = 0;

  if ((info.flags & kMinfsFlagFVM) == 0) {
    blk_t non_dat_blocks;
    blk_t journal_blocks = 0;

    info.ibm_block = 8;
    info.abm_block = info.ibm_block + fbl::round_up(ibmblks, 8u);

    for (uint32_t alloc_bitmap_rounded = 8; alloc_bitmap_rounded < blocks;
         alloc_bitmap_rounded += 8) {
      // Increment bitmap blocks by 8, since we will always round this value up to 8.
      ZX_ASSERT(alloc_bitmap_rounded % 8 == 0);

      info.ino_block = info.abm_block + alloc_bitmap_rounded;

      // Calculate the journal size based on other metadata structures.
      TransactionLimits limits(info);
      journal_blocks = limits.GetRecommendedIntegrityBlocks();

      non_dat_blocks = 8 + fbl::round_up(ibmblks, 8u) + alloc_bitmap_rounded + inoblks;

      // If the recommended journal count is too high, try using the minimum instead.
      if (non_dat_blocks + journal_blocks >= blocks) {
        journal_blocks = limits.GetMinimumIntegrityBlocks();
      }

      non_dat_blocks += journal_blocks;
      if (non_dat_blocks >= blocks) {
        FX_LOGS(ERROR) << "mkfs: Partition size ("
                       << static_cast<uint64_t>(blocks) * info.BlockSize()
                       << " bytes) is too small";
        return zx::error(ZX_ERR_INVALID_ARGS);
      }

      info.block_count = blocks - non_dat_blocks;
      // Calculate the exact number of bitmap blocks needed to track this many data blocks.
      abmblks = (info.block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;

      if (alloc_bitmap_rounded >= abmblks) {
        // It is possible that the abmblks value will actually bring us back to the next
        // lowest tier of 8-rounded values. This means we may have 8 blocks allocated for
        // the block bitmap which will never actually be used. This is not ideal, but is
        // expected, and should only happen for very particular block counts.
        break;
      }
    }

    info.integrity_start_block = info.ino_block + inoblks;
    info.dat_block = info.integrity_start_block + journal_blocks;
  } else {
    info.block_count = blocks;
    abmblks = (info.block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
    info.ibm_block = kFVMBlockInodeBmStart;
    info.abm_block = kFVMBlockDataBmStart;
    info.ino_block = kFVMBlockInodeStart;
    info.integrity_start_block = kFvmSuperblockBackup;
    info.dat_block = kFVMBlockDataStart;
  }
  info.oldest_minor_version = kMinfsCurrentMinorVersion;
  DumpInfo(info);

  RawBitmap abm;
  RawBitmap ibm;

  // By allocating the bitmap and then shrinking it, we keep the underlying
  // storage a block multiple but ensure we can't allocate beyond the last
  // real block or inode.
  if (zx_status_t status = abm.Reset(fbl::round_up(info.block_count, kMinfsBlockBits));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "mkfs: Failed to allocate block bitmap: " << status;
    return zx::error(status);
  }
  if (zx_status_t status = ibm.Reset(fbl::round_up(info.inode_count, kMinfsBlockBits));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "mkfs: Failed to allocate inode bitmap: " << status;
    return zx::error(status);
  }
  if (zx_status_t status = abm.Shrink(info.block_count); status != ZX_OK) {
    FX_LOGS(ERROR) << "mkfs: Failed to shrink block bitmap: " << status;
    return zx::error(status);
  }
  if (zx_status_t status = ibm.Shrink(info.inode_count); status != ZX_OK) {
    FX_LOGS(ERROR) << "mkfs: Failed to shrink inode bitmap: " << status;
    return zx::error(status);
  }

  // Write rootdir
  uint8_t blk[info.BlockSize()];
  memset(blk, 0, sizeof(blk));
  InitializeDirectory(blk, kMinfsRootIno, kMinfsRootIno);
  if (auto status = bc->Writeblk(info.dat_block + 1, blk); status.is_error()) {
    FX_LOGS(ERROR) << "mkfs: Failed to write root directory: " << status.error_value();
    return status.take_error();
  }

  // Update inode bitmap
  ibm.Set(0, 1);
  ibm.Set(kMinfsRootIno, kMinfsRootIno + 1);
  info.alloc_inode_count += 2;

  // update block bitmap:
  // Reserve the 0th data block (as a 'null' value)
  // Reserve the 1st data block (for root directory)
  abm.Set(0, 2);
  info.alloc_block_count += 2;

  // Write allocation bitmap
  for (uint32_t n = 0; n < abmblks; n++) {
    void* bmdata = fs::GetBlock(info.BlockSize(), abm.StorageUnsafe()->GetData(), n);
    memcpy(blk, bmdata, info.BlockSize());
    if (auto status = bc->Writeblk(info.abm_block + n, blk); status.is_error()) {
      return status.take_error();
    }
  }

  // Write inode bitmap
  for (uint32_t n = 0; n < ibmblks; n++) {
    void* bmdata = fs::GetBlock(info.BlockSize(), ibm.StorageUnsafe()->GetData(), n);
    memcpy(blk, bmdata, info.BlockSize());
    if (auto status = bc->Writeblk(info.ibm_block + n, blk); status.is_error()) {
      return status.take_error();
    }
  }

  // Write inodes
  memset(blk, 0, sizeof(blk));
  for (uint32_t n = 0; n < inoblks; n++) {
    if (auto status = bc->Writeblk(info.ino_block + n, blk); status.is_error()) {
      return status.take_error();
    }
  }

  // Setup root inode
  Inode* ino = reinterpret_cast<Inode*>(blk);
  ino[kMinfsRootIno].magic = kMinfsMagicDir;
  ino[kMinfsRootIno].size = info.BlockSize();
  ino[kMinfsRootIno].block_count = 1;
  ino[kMinfsRootIno].link_count = 2;
  ino[kMinfsRootIno].dirent_count = 2;
  ino[kMinfsRootIno].dnum[0] = 1;
  ino[kMinfsRootIno].create_time = GetTimeUTC();
  (void)bc->Writeblk(info.ino_block, blk);

  info.generation_count = 0;
  UpdateChecksum(&info);

  // Write superblock info to disk.
  (void)bc->Writeblk(kSuperblockStart, &info);

  // Write backup superblock info to disk.
  if ((info.flags & kMinfsFlagFVM) == 0) {
    (void)bc->Writeblk(kNonFvmSuperblockBackup, &info);
  } else {
    (void)bc->Writeblk(kFvmSuperblockBackup, &info);
  }

  fs::WriteBlocksFn write_blocks_fn = [bc, info](cpp20::span<const uint8_t> buffer,
                                                 uint64_t block_offset, uint64_t block_count) {
    ZX_ASSERT((block_count + block_offset) <= JournalBlocks(info));
    ZX_ASSERT(buffer.size() >= (block_count * info.BlockSize()));
    auto data = buffer.data();
    while (block_count > 0) {
      auto status = bc->Writeblk(static_cast<blk_t>(JournalStartBlock(info) + block_offset), data);
      if (status.is_error()) {
        return status.status_value();
      }
      block_offset = safemath::CheckAdd(block_offset, 1).ValueOrDie();
      block_count = safemath::CheckSub(block_count, 1).ValueOrDie();
      data += info.BlockSize();
    }
    return ZX_OK;
  };
  ZX_ASSERT(fs::MakeJournal(JournalBlocks(info), write_blocks_fn) == ZX_OK);

#ifdef __Fuchsia__
  fvm_cleanup.cancel();
#endif

  return bc->Sync();
}

zx::status<> Minfs::ReadDat(blk_t bno, void* data) {
#ifdef __Fuchsia__
  return bc_->Readblk(Info().dat_block + bno, data);
#else
  return ReadBlk(bno, offsets_.DatStartBlock(), offsets_.DatBlockCount(), Info().block_count, data);
#endif
}

zx_status_t Minfs::ReadBlock(blk_t start_block_num, void* out_data) const {
  return bc_->Readblk(start_block_num, out_data).status_value();
}

#ifndef __Fuchsia__
zx::status<> Minfs::ReadBlk(blk_t bno, blk_t start, blk_t soft_max, blk_t hard_max,
                            void* data) const {
  if (bno >= hard_max) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  if (bno >= soft_max) {
    memset(data, 0, BlockSize());
    return zx::ok();
  }

  return bc_->Readblk(start + bno, data);
}

zx::status<std::unique_ptr<minfs::Bcache>> CreateBcacheFromFd(
    fbl::unique_fd fd, off_t start, off_t end, const fbl::Vector<size_t>& extent_lengths) {
  if (start >= end) {
    FX_LOGS(ERROR) << "Insufficient space allocated";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (extent_lengths.size() != kExtentCount) {
    FX_LOGS(ERROR) << "invalid number of extents : " << extent_lengths.size();
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  struct stat s;
  if (fstat(fd.get(), &s) < 0) {
    FX_LOGS(ERROR) << "minfs could not find end of file/device";
    return zx::error(ZX_ERR_IO);
  }

  if (s.st_size < end) {
    FX_LOGS(ERROR) << "invalid file size";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  size_t size = (end - start) / minfs::kMinfsBlockSize;

  auto bc_or = minfs::Bcache::Create(std::move(fd), static_cast<uint32_t>(size));
  if (bc_or.is_error()) {
    FX_LOGS(ERROR) << "cannot create block cache: " << bc_or.error_value();
    return bc_or.take_error();
  }

  if (auto status = bc_or->SetSparse(start, extent_lengths); status.is_error()) {
    FX_LOGS(ERROR) << "Bcache is already sparse: " << status.error_value();
    return status.take_error();
  }

  return zx::ok(std::move(bc_or.value()));
}

zx::status<uint64_t> SparseUsedDataSize(fbl::unique_fd fd, off_t start, off_t end,
                                        const fbl::Vector<size_t>& extent_lengths) {
  auto bc_or = CreateBcacheFromFd(std::move(fd), start, end, extent_lengths);
  if (bc_or.is_error()) {
    return bc_or.take_error();
  }
  return UsedDataSize(bc_or.value());
}

zx::status<uint64_t> SparseUsedInodes(fbl::unique_fd fd, off_t start, off_t end,
                                      const fbl::Vector<size_t>& extent_lengths) {
  auto bc_or = CreateBcacheFromFd(std::move(fd), start, end, extent_lengths);
  if (bc_or.is_error()) {
    return bc_or.take_error();
  }
  return UsedInodes(bc_or.value());
}

zx::status<uint64_t> SparseUsedSize(fbl::unique_fd fd, off_t start, off_t end,
                                    const fbl::Vector<size_t>& extent_lengths) {
  auto bc_or = CreateBcacheFromFd(std::move(fd), start, end, extent_lengths);
  if (bc_or.is_error()) {
    return bc_or.take_error();
  }
  return UsedSize(bc_or.value());
}

#endif

#ifdef __Fuchsia__
fbl::Vector<BlockRegion> Minfs::GetAllocatedRegions() const {
  return block_allocator_->GetAllocatedRegions();
}
#endif

}  // namespace minfs
