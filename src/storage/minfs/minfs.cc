// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/minfs.h"

#include <fcntl.h>
#include <inttypes.h>
#include <lib/cksum.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <limits>
#include <memory>

#include <bitmap/raw-bitmap.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fs/journal/initializer.h>
#include <fs/trace.h>
#include <safemath/checked_math.h>
#ifdef __Fuchsia__
#include <fuchsia/minfs/llcpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/cksum.h>
#include <lib/zx/event.h>

#include <fbl/auto_lock.h>
#include <fs/journal/header_view.h>
#include <fs/journal/journal.h>
#include <fs/journal/replay.h>
#include <fs/pseudo_dir.h>
#include <fvm/client.h>
#include <storage/buffer/owned_vmoid.h>
#endif

#include <utility>

#include <fs/journal/format.h>

#include "src/storage/minfs/file.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/minfs_private.h"

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
zx_status_t CheckSlices(const Superblock* info, size_t blocks_per_slice,
                        block_client::BlockDevice* device, bool repair_slices) {
  fuchsia_hardware_block_volume_VolumeInfo fvm_info;
  zx_status_t status = device->VolumeQuery(&fvm_info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: unable to query FVM :%d\n", status);
    return ZX_ERR_UNAVAILABLE;
  }

  if (info->slice_size != fvm_info.slice_size) {
    FS_TRACE_ERROR("minfs: slice size %u did not match expected size %lu\n", info->slice_size,
                   fvm_info.slice_size);
    return ZX_ERR_BAD_STATE;
  }

  size_t expected_count[4];
  expected_count[0] = info->ibm_slices;
  expected_count[1] = info->abm_slices;
  expected_count[2] = info->ino_slices;
  expected_count[3] = info->dat_slices;

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
    FS_TRACE_ERROR("minfs: unable to query FVM: %d\n", status);
    return ZX_ERR_UNAVAILABLE;
  }

  if (ranges_count != request.count) {
    FS_TRACE_ERROR("minfs: requested FVM range :%lu does not match received: %lu\n", request.count,
                   ranges_count);
    return ZX_ERR_BAD_STATE;
  }

  for (uint32_t i = 0; i < request.count; i++) {
    size_t minfs_count = expected_count[i];
    size_t fvm_count = ranges[i].count;

    if (!ranges[i].allocated || fvm_count < minfs_count) {
      // Currently, since Minfs can only grow new slices (except for the one instance below), it
      // should not be possible for the FVM to report a slice size smaller than what is reported by
      // Minfs. In this case, automatically fail without trying to resolve the situation, as it is
      // possible that Minfs structures are allocated in the slices that have been lost.
      FS_TRACE_ERROR("minfs: mismatched slice count\n");
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    if (repair_slices && fvm_count > minfs_count) {
      // If FVM reports more slices than we expect, try to free remainder.
      extend_request_t shrink;
      shrink.length = fvm_count - minfs_count;
      shrink.offset = request.vslice_start[i] + minfs_count;
      if ((status = device->VolumeShrink(shrink.offset, shrink.length)) != ZX_OK) {
        FS_TRACE_ERROR("minfs: Unable to shrink to expected size, status: %d\n", status);
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
    }
  }
  return ZX_OK;
}

// Issues a sync to the journal's background thread and waits for it to complete.
zx_status_t BlockingSync(fs::Journal* journal) {
  zx_status_t sync_status = ZX_OK;
  sync_completion_t sync_completion = {};
  journal->schedule_task(
      journal->Sync().then([&sync_status, &sync_completion](fit::result<void, zx_status_t>& a) {
        sync_status = a.is_ok() ? ZX_OK : a.error();
        sync_completion_signal(&sync_completion);
        return fit::ok();
      }));
  zx_status_t status = sync_completion_wait(&sync_completion, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    return status;
  }
  return sync_status;
}

// Setups the superblock based on the mount options and the underlying device.
// It can be called when not loaded on top of FVM, in which case this function
// will do nothing.
zx_status_t CreateFvmData(const MountOptions& options, Superblock* info,
                          block_client::BlockDevice* device) {
  fuchsia_hardware_block_volume_VolumeInfo fvm_info;
  if (device->VolumeQuery(&fvm_info) != ZX_OK) {
    return ZX_OK;
  }

  info->slice_size = static_cast<uint32_t>(fvm_info.slice_size);
  SetMinfsFlagFvm(*info);

  if (info->slice_size % info->BlockSize()) {
    FS_TRACE_ERROR("minfs mkfs: Slice size not multiple of minfs block: %u\n", info->slice_size);
    return ZX_ERR_IO_INVALID;
  }

  const size_t kBlocksPerSlice = info->slice_size / info->BlockSize();
  extend_request_t request;
  request.length = 1;
  request.offset = kFVMBlockInodeBmStart / kBlocksPerSlice;
  zx_status_t status = fvm::ResetAllSlices(device);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs mkfs: Failed to reset FVM slices: %d\n", status);
    return status;
  }
  if ((status = device->VolumeExtend(request.offset, request.length)) != ZX_OK) {
    FS_TRACE_ERROR("minfs mkfs: Failed to allocate inode bitmap: %d\n", status);
    return status;
  }
  info->ibm_slices = 1;
  request.offset = kFVMBlockDataBmStart / kBlocksPerSlice;
  if ((status = device->VolumeExtend(request.offset, request.length)) != ZX_OK) {
    FS_TRACE_ERROR("minfs mkfs: Failed to allocate data bitmap: %d\n", status);
    return status;
  }
  info->abm_slices = 1;
  request.offset = kFVMBlockInodeStart / kBlocksPerSlice;
  if ((status = device->VolumeExtend(request.offset, request.length)) != ZX_OK) {
    FS_TRACE_ERROR("minfs mkfs: Failed to allocate inode table: %d\n", status);
    return status;
  }
  info->ino_slices = 1;

  TransactionLimits limits(*info);
  blk_t journal_blocks = limits.GetRecommendedIntegrityBlocks();
  request.length = fbl::round_up(journal_blocks, kBlocksPerSlice) / kBlocksPerSlice;
  request.offset = kFVMBlockJournalStart / kBlocksPerSlice;
  if ((status = device->VolumeExtend(request.offset, request.length)) != ZX_OK) {
    FS_TRACE_ERROR("minfs mkfs: Failed to allocate journal blocks: %d\n", status);
    return status;
  }
  info->integrity_slices = static_cast<blk_t>(request.length);

  ZX_ASSERT(options.fvm_data_slices > 0);
  request.length = options.fvm_data_slices;
  request.offset = kFVMBlockDataStart / kBlocksPerSlice;
  if ((status = device->VolumeExtend(request.offset, request.length)) != ZX_OK) {
    FS_TRACE_ERROR("minfs mkfs: Failed to allocate data blocks: %d\n", status);
    return status;
  }
  info->dat_slices = options.fvm_data_slices;

  return ZX_OK;
}
#endif

// Verifies that the allocated slices are sufficient to hold the allocated data
// structures of the filesystem.
zx_status_t VerifySlicesSize(const Superblock* info, const TransactionLimits& limits,
                             size_t blocks_per_slice) {
  size_t ibm_blocks_needed = (info->inode_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
  size_t ibm_blocks_allocated = info->ibm_slices * blocks_per_slice;
  if (ibm_blocks_needed > ibm_blocks_allocated) {
    FS_TRACE_ERROR("minfs: Not enough slices for inode bitmap\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (ibm_blocks_allocated + info->ibm_block >= info->abm_block) {
    FS_TRACE_ERROR("minfs: Inode bitmap collides into block bitmap\n");
    return ZX_ERR_INVALID_ARGS;
  }

  size_t abm_blocks_needed = (info->block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;
  size_t abm_blocks_allocated = info->abm_slices * blocks_per_slice;
  if (abm_blocks_needed > abm_blocks_allocated) {
    FS_TRACE_ERROR("minfs: Not enough slices for block bitmap\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (abm_blocks_allocated + info->abm_block >= info->ino_block) {
    FS_TRACE_ERROR("minfs: Block bitmap collides with inode table\n");
    return ZX_ERR_INVALID_ARGS;
  }

  size_t ino_blocks_needed = (info->inode_count + kMinfsInodesPerBlock - 1) / kMinfsInodesPerBlock;
  size_t ino_blocks_allocated = info->ino_slices * blocks_per_slice;
  if (ino_blocks_needed > ino_blocks_allocated) {
    FS_TRACE_ERROR("minfs: Not enough slices for inode table\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (ino_blocks_allocated + info->ino_block >= info->integrity_start_block) {
    FS_TRACE_ERROR("minfs: Inode table collides with data blocks\n");
    return ZX_ERR_INVALID_ARGS;
  }

  size_t journal_blocks_needed = limits.GetMinimumIntegrityBlocks();
  size_t journal_blocks_allocated = info->integrity_slices * blocks_per_slice;
  if (journal_blocks_needed > journal_blocks_allocated) {
    FS_TRACE_ERROR("minfs: Not enough slices for journal\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (journal_blocks_allocated + info->integrity_start_block > info->dat_block) {
    FS_TRACE_ERROR("minfs: Journal collides with data blocks\n");
    return ZX_ERR_INVALID_ARGS;
  }

  size_t dat_blocks_needed = info->block_count;
  size_t dat_blocks_allocated = info->dat_slices * blocks_per_slice;
  if (dat_blocks_needed > dat_blocks_allocated) {
    FS_TRACE_ERROR("minfs: Not enough slices for data blocks\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (dat_blocks_allocated + info->dat_block > std::numeric_limits<blk_t>::max()) {
    FS_TRACE_ERROR("minfs: Data blocks overflow blk_t\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (dat_blocks_needed <= 1) {
    FS_TRACE_ERROR("minfs: Not enough data blocks\n");
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

// Fuses "reading the superblock from storage" with "correcting if it is wrong".
zx_status_t LoadSuperblockWithRepair(Bcache* bc, bool repair, Superblock* out_info) {
  zx_status_t status = LoadSuperblock(bc, out_info);
  if (status != ZX_OK) {
    if (!repair) {
      FS_TRACE_ERROR("minfs: Cannot load superblock; not attempting to repair\n");
      return status;
    }
    FS_TRACE_WARN("minfs: Attempting to repair superblock\n");

#ifdef __Fuchsia__
    status = RepairSuperblock(bc, bc->device(), bc->Maxblk(), out_info);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("minfs: Unable to repair corrupt filesystem.\n");
      return status;
    }
#else
    return ZX_ERR_NOT_SUPPORTED;
#endif
  }
  return ZX_OK;
}

#ifdef __Fuchsia__

// Replays the journal and reloads the superblock (it may have been present in the journal).
//
// |info| is both an input and output parameter; it may be overwritten.
zx_status_t ReplayJournalReloadSuperblock(Bcache* bc, Superblock* info,
                                          fs::JournalSuperblock* out_journal_superblock) {
  zx_status_t status = ReplayJournal(bc, *info, out_journal_superblock);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: Cannot replay journal\n");
    return status;
  }
  // Re-load the superblock after replaying the journal.
  return LoadSuperblock(bc, info);
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
  FS_TRACE_DEBUG("minfs: magic0:  %10" PRIu64 "\n", info.magic0);
  FS_TRACE_DEBUG("minfs: magic1:  %10" PRIu64 "\n", info.magic1);
  FS_TRACE_DEBUG("minfs: format version:  %10u\n", info.format_version);
  FS_TRACE_DEBUG("minfs: data blocks:  %10u (size %u)\n", info.block_count, info.block_size);
  FS_TRACE_DEBUG("minfs: inodes:  %10u (size %u)\n", info.inode_count, info.inode_size);
  FS_TRACE_DEBUG("minfs: allocated blocks  @ %10u\n", info.alloc_block_count);
  FS_TRACE_DEBUG("minfs: allocated inodes  @ %10u\n", info.alloc_inode_count);
  FS_TRACE_DEBUG("minfs: inode bitmap @ %10u\n", info.ibm_block);
  FS_TRACE_DEBUG("minfs: alloc bitmap @ %10u\n", info.abm_block);
  FS_TRACE_DEBUG("minfs: inode table  @ %10u\n", info.ino_block);
  FS_TRACE_DEBUG("minfs: integrity start block  @ %10u\n", info.integrity_start_block);
  FS_TRACE_DEBUG("minfs: data blocks  @ %10u\n", info.dat_block);
  FS_TRACE_DEBUG("minfs: FVM-aware: %s\n", (info.flags & kMinfsFlagFVM) ? "YES" : "NO");
  FS_TRACE_DEBUG("minfs: checksum:  %10u\n", info.checksum);
  FS_TRACE_DEBUG("minfs: generation count:  %10u\n", info.generation_count);
  FS_TRACE_DEBUG("minfs: oldest_revision:  %10u\n", info.oldest_revision);
  FS_TRACE_DEBUG("minfs: slice_size: %u\n", info.slice_size);
  FS_TRACE_DEBUG("minfs: ibm_slices: %u\n", info.ibm_slices);
  FS_TRACE_DEBUG("minfs: abm_slices: %u\n", info.abm_slices);
  FS_TRACE_DEBUG("minfs: ino_slices: %u\n", info.ino_slices);
  FS_TRACE_DEBUG("minfs: integrity_slices: %u\n", info.integrity_slices);
  FS_TRACE_DEBUG("minfs: dat_slices: %u\n", info.integrity_slices);
}

void DumpInode(const Inode* inode, ino_t ino) {
  FS_TRACE_DEBUG("inode[%u]: magic:  %10u\n", ino, inode->magic);
  FS_TRACE_DEBUG("inode[%u]: size:   %10u\n", ino, inode->size);
  FS_TRACE_DEBUG("inode[%u]: blocks: %10u\n", ino, inode->block_count);
  FS_TRACE_DEBUG("inode[%u]: links:  %10u\n", ino, inode->link_count);
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
zx_status_t CheckSuperblock(const Superblock* info, block_client::BlockDevice* device,
                            uint32_t max_blocks) {
#else
zx_status_t CheckSuperblock(const Superblock* info, uint32_t max_blocks) {
#endif
  DumpInfo(*info);
  if ((info->magic0 != kMinfsMagic0) || (info->magic1 != kMinfsMagic1)) {
    FS_TRACE_ERROR("minfs: bad magic: %08" PRIi64 ". Minfs magic: %08" PRIu64 "\n", info->magic0,
                   kMinfsMagic0);
    return ZX_ERR_WRONG_TYPE;
  }
  if (info->format_version != kMinfsCurrentFormatVersion) {
    FS_TRACE_ERROR("minfs: FS major version: %08x. Driver major version: %08x\n",
                   info->format_version, kMinfsCurrentFormatVersion);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if ((info->block_size != kMinfsBlockSize) || (info->inode_size != kMinfsInodeSize)) {
    FS_TRACE_ERROR("minfs: bsz/isz %u/%u unsupported\n", info->block_size, info->inode_size);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  Superblock chksum_info;
  memcpy(&chksum_info, info, sizeof(chksum_info));
  chksum_info.checksum = 0;
  uint32_t checksum = crc32(0, reinterpret_cast<const uint8_t*>(&chksum_info), sizeof(chksum_info));
  if (info->checksum != checksum) {
    FS_TRACE_ERROR("minfs: bad checksum: %u. Expected: %u\n", info->checksum, checksum);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  TransactionLimits limits(*info);
  if ((info->flags & kMinfsFlagFVM) == 0) {
    if (info->dat_block + info->block_count != max_blocks) {
      FS_TRACE_ERROR("minfs: too large for device\n");
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    if (info->dat_block - info->integrity_start_block < limits.GetMinimumIntegrityBlocks()) {
      FS_TRACE_ERROR("minfs: journal too small\n");
      return ZX_ERR_BAD_STATE;
    }
  } else {
    const size_t kBlocksPerSlice = info->slice_size / info->BlockSize();
    zx_status_t status;
#ifdef __Fuchsia__
    status = CheckSlices(info, kBlocksPerSlice, device, /*repair_slices=*/false);
    if (status != ZX_OK) {
      return status;
    }
#endif
    status = VerifySlicesSize(info, limits, kBlocksPerSlice);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
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

zx_status_t Minfs::BeginTransaction(size_t reserve_inodes, size_t reserve_blocks,
                                    std::unique_ptr<Transaction>* out) {
  ZX_DEBUG_ASSERT(reserve_inodes <= TransactionLimits::kMaxInodeBitmapBlocks);
#ifdef __Fuchsia__
  if (journal_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  if (!journal_->IsWritebackEnabled()) {
    return ZX_ERR_IO_REFUSED;
  }

  // TODO(planders): Once we are splitting up write transactions, assert this on host as well.
  ZX_DEBUG_ASSERT(reserve_blocks <= limits_.GetMaximumDataBlocks());
#endif
  // Reserve blocks from allocators before returning WritebackWork to client.
  return Transaction::Create(this, reserve_inodes, reserve_blocks, inodes_.get(), out);
}

#ifdef __Fuchsia__
void Minfs::EnqueueCallback(SyncCallback callback) {
  if (callback) {
    journal_->schedule_task(journal_->Sync().then(
        [closure = std::move(callback)](
            fit::result<void, zx_status_t>& result) mutable -> fit::result<void, zx_status_t> {
          if (result.is_ok()) {
            closure(ZX_OK);
          } else {
            closure(result.error());
          }
          return fit::ok();
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

  void operator()([[maybe_unused]] const fit::result<void, zx_status_t>& dont_care) {
    object_.reset();
  }

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
  //    won't be possible to use free inodes until the next transaction. This probably can't happen
  //    anyway.
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
    FS_TRACE_ERROR("minfs: CommitTransaction failed: %s\n", zx_status_get_string(status));
  }

  if (!journal_sync_task_.is_pending()) {
    // During mount, there isn't a dispatcher, so we won't queue a flush, but that won't matter
    // since the only changes will be things like whether the volume is clean and it doesn't matter
    // if they're not persisted.
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
    std::unique_ptr<Bcache> bcache;
    ZX_ASSERT(Bcache::Create(bc_->device(), bc_->Maxblk(), &bcache) == ZX_OK);
    ZX_ASSERT(Fsck(std::move(bcache), FsckOptions{.read_only = true, .quiet = true}, &bcache) ==
              ZX_OK);
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
  EnqueueCallback(std::move(closure));
}
#endif

#ifdef __Fuchsia__
Minfs::Minfs(std::unique_ptr<Bcache> bc, std::unique_ptr<SuperblockManager> sb,
             std::unique_ptr<Allocator> block_allocator, std::unique_ptr<InodeManager> inodes,
             uint64_t fs_id, const MountOptions& mount_options)
    : bc_(std::move(bc)),
      sb_(std::move(sb)),
      block_allocator_(std::move(block_allocator)),
      inodes_(std::move(inodes)),
      fs_id_(fs_id),
      journal_sync_task_([this]() { Sync(); }),
      limits_(sb_->Info()),
      mount_options_(mount_options) {}
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

#ifdef __Fuchsia__
zx_status_t Minfs::FVMQuery(fuchsia_hardware_block_volume_VolumeInfo* info) const {
  if (!(Info().flags & kMinfsFlagFVM)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return bc_->device()->VolumeQuery(info);
}
#endif

zx_status_t Minfs::InoFree(Transaction* transaction, VnodeMinfs* vn) {
  TRACE_DURATION("minfs", "Minfs::InoFree", "ino", vn->GetIno());

#ifdef __Fuchsia__
  vn->CancelPendingWriteback();
#endif

  inodes_->Free(transaction, vn->GetIno());

  zx_status_t status = vn->BlocksShrink(transaction, 0);
  if (status != ZX_OK)
    return status;
  vn->MarkPurged();
  InodeUpdate(transaction, vn->GetIno(), vn->GetInode());

  ZX_DEBUG_ASSERT(vn->GetInode()->block_count == 0);
  ZX_DEBUG_ASSERT(vn->IsUnlinked());
  return ZX_OK;
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

zx_status_t Minfs::PurgeUnlinked() {
  ino_t last_ino = 0;
  ino_t next_ino = Info().unlinked_head;
  ino_t unlinked_count = 0;

  if (next_ino == 0) {
    ZX_DEBUG_ASSERT(Info().unlinked_tail == 0);
    return ZX_OK;
  }

  // Loop through the unlinked list and free all allocated resources.
  fbl::RefPtr<VnodeMinfs> vn;
  VnodeMinfs::Recreate(this, next_ino, &vn);
  ZX_DEBUG_ASSERT(vn->GetInode()->last_inode == 0);

  do {
    zx_status_t status;
    std::unique_ptr<Transaction> transaction;
    if ((status = BeginTransaction(0, 0, &transaction)) != ZX_OK) {
      return status;
    }

    ZX_DEBUG_ASSERT(vn->GetInode()->link_count == 0);

    if ((status = InoFree(transaction.get(), vn.get())) != ZX_OK) {
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
      InodeUpdate(transaction.get(), next_ino, vn->GetInode());
    }
    CommitTransaction(std::move(transaction));
    unlinked_count++;
  } while (next_ino != 0);

  ZX_DEBUG_ASSERT(Info().unlinked_head == 0);
  ZX_DEBUG_ASSERT(Info().unlinked_tail == 0);

  if (!mount_options_.quiet) {
    FS_TRACE_WARN("minfs: Found and purged %u unlinked vnode(s) on mount\n", unlinked_count);
  }

  return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t Minfs::CreateFsId(uint64_t* out) {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    return status;
  }
  zx_info_handle_basic_t info;
  status = event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  *out = info.koid;
  return ZX_OK;
}

zx_status_t Minfs::UpdateCleanBitAndOldestRevision(bool is_clean) {
  std::unique_ptr<Transaction> transaction;
  zx_status_t status = BeginTransaction(0, 0, &transaction);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: failed to %s clean flag: %d\n", is_clean ? "set" : "unset", status);
    return status;
  }
  if (kMinfsCurrentRevision < Info().oldest_revision) {
    sb_->MutableInfo()->oldest_revision = kMinfsCurrentRevision;
  }
  UpdateFlags(transaction.get(), kMinfsFlagClean, is_clean);
  CommitTransaction(std::move(transaction));
  // Mount/unmount marks filesystem as dirty/clean. When we called UpdateFlags
  // above, the underlying subsystems may complete the IO asynchronously. But
  // these operations(and any other operations issued before) should be
  // persisted to final location before we allow any other operation to the
  // filesystem or before we return completion status to the caller.
  return BlockingSync(journal_.get());
}

void Minfs::StopWriteback() {
  // Minfs already terminated.
  if (!bc_) {
    return;
  }

  if (IsReadonly() == false) {
    // Ignore errors here since there is nothing we can do.
    [[maybe_unused]] zx_status_t status = UpdateCleanBitAndOldestRevision(/*is_clean=*/true);
  }

  journal_ = nullptr;
  bc_->Sync();
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

zx_status_t Minfs::VnodeNew(Transaction* transaction, fbl::RefPtr<VnodeMinfs>* out, uint32_t type) {
  TRACE_DURATION("minfs", "Minfs::VnodeNew");
  if ((type != kMinfsTypeFile) && (type != kMinfsTypeDir)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<VnodeMinfs> vn;

  // Allocate the in-memory vnode
  VnodeMinfs::Allocate(this, type, &vn);

  // Allocate the on-disk inode
  ino_t ino;
  InoNew(transaction, vn->GetInode(), &ino);
  vn->SetIno(ino);
  VnodeInsert(vn.get());
  *out = std::move(vn);
  return ZX_OK;
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

zx_status_t Minfs::VnodeGet(fbl::RefPtr<VnodeMinfs>* out, ino_t ino) {
  TRACE_DURATION("minfs", "Minfs::VnodeGet", "ino", ino);
  if ((ino < 1) || (ino >= Info().inode_count)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fs::Ticker ticker(StartTicker());

  fbl::RefPtr<VnodeMinfs> vn = VnodeLookup(ino);
  if (vn != nullptr) {
    *out = std::move(vn);
    UpdateOpenMetrics(/* cache_hit= */ true, ticker.End());
    return ZX_OK;
  }

  VnodeMinfs::Recreate(this, ino, &vn);

  if (vn->IsUnlinked()) {
    // If a vnode we have recreated from disk is unlinked, something has gone wrong during the
    // unlink process and our filesystem is now in an inconsistent state. In order to avoid
    // further inconsistencies, prohibit access to this vnode.
    FS_TRACE_WARN("minfs: Attempted to load unlinked vnode %u\n", ino);
    return ZX_ERR_BAD_STATE;
  }

  VnodeInsert(vn.get());
  *out = std::move(vn);
  UpdateOpenMetrics(/* cache_hit= */ false, ticker.End());
  return ZX_OK;
}

// Allocate a new data block from the block bitmap.
void Minfs::BlockNew(PendingWork* transaction, blk_t* out_bno) const {
  size_t allocated_bno = transaction->AllocateBlock();
  *out_bno = static_cast<blk_t>(allocated_bno);
  ValidateBno(*out_bno);
}

bool Minfs::IsReadonly() {
#ifdef __Fuchsia__
  fbl::AutoLock lock(&vfs_lock_);
#endif
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

zx_status_t Minfs::ReadInitialBlocks(const Superblock& info, std::unique_ptr<Bcache> bc,
                                     std::unique_ptr<SuperblockManager> sb,
                                     const MountOptions& mount_options,
                                     std::unique_ptr<Minfs>* out_minfs) {
#ifdef __Fuchsia__
  const blk_t abm_start_block = sb->Info().abm_block;
  const blk_t ibm_start_block = sb->Info().ibm_block;
  const blk_t ino_start_block = sb->Info().ino_block;
#else
  BlockOffsets offsets(*bc, *sb);
  const blk_t abm_start_block = offsets.AbmStartBlock();
  const blk_t ibm_start_block = offsets.IbmStartBlock();
  const blk_t ino_start_block = offsets.InoStartBlock();
#endif

  fs::BufferedOperationsBuilder builder;

  // Block Bitmap allocator initialization.
  AllocatorFvmMetadata block_allocator_fvm =
      AllocatorFvmMetadata(sb.get(), SuperblockAllocatorAccess::Blocks());
  AllocatorMetadata block_allocator_meta = AllocatorMetadata(
      info.dat_block, abm_start_block, (info.flags & kMinfsFlagFVM) != 0,
      std::move(block_allocator_fvm), sb.get(), SuperblockAllocatorAccess::Blocks());

  std::unique_ptr<PersistentStorage> storage(
#ifdef __Fuchsia__
      new PersistentStorage(bc->device(), sb.get(), sb->Info().BlockSize(), nullptr,
                            std::move(block_allocator_meta), sb->BlockSize()));
#else
      new PersistentStorage(sb.get(), sb->Info().BlockSize(), nullptr,
                            std::move(block_allocator_meta), sb->BlockSize()));
#endif

  std::unique_ptr<Allocator> block_allocator;
  zx_status_t status = Allocator::Create(&builder, std::move(storage), &block_allocator);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Minfs::Create failed to initialize block allocator: %d\n", status);
    return status;
  }

  // Inode Bitmap allocator initialization.
  AllocatorFvmMetadata inode_allocator_fvm =
      AllocatorFvmMetadata(sb.get(), SuperblockAllocatorAccess::Inodes());
  AllocatorMetadata inode_allocator_meta = AllocatorMetadata(
      ino_start_block, ibm_start_block, (info.flags & kMinfsFlagFVM) != 0,
      std::move(inode_allocator_fvm), sb.get(), SuperblockAllocatorAccess::Inodes());

  std::unique_ptr<InodeManager> inodes;
#ifdef __Fuchsia__
  status = InodeManager::Create(bc->device(), sb.get(), &builder, std::move(inode_allocator_meta),
                                ino_start_block, info.inode_count, &inodes);
#else
  status = InodeManager::Create(bc.get(), sb.get(), &builder, std::move(inode_allocator_meta),
                                ino_start_block, info.inode_count, &inodes);
#endif
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Minfs::Create failed to initialize inodes: %d\n", status);
    return status;
  }

  status = bc->RunRequests(builder.TakeOperations());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Minfs::Create failed to read initial blocks: %d\n", status);
    return status;
  }

#ifdef __Fuchsia__
  uint64_t id;
  status = Minfs::CreateFsId(&id);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: failed to create fs_id: %d\n", status);
    return status;
  }

  *out_minfs =
      std::unique_ptr<Minfs>(new Minfs(std::move(bc), std::move(sb), std::move(block_allocator),
                                       std::move(inodes), id, mount_options));
#else
  *out_minfs =
      std::unique_ptr<Minfs>(new Minfs(std::move(bc), std::move(sb), std::move(block_allocator),
                                       std::move(inodes), offsets, mount_options));
#endif
  return ZX_OK;
}

zx_status_t Minfs::Create(std::unique_ptr<Bcache> bc, const MountOptions& options,
                          std::unique_ptr<Minfs>* out) {
  // Read the superblock before replaying the journal.
  Superblock info;
  zx_status_t status = LoadSuperblockWithRepair(bc.get(), options.repair_filesystem, &info);
  if (status != ZX_OK) {
    return status;
  }

#ifdef __Fuchsia__
  if ((info.flags & kMinfsFlagClean) == 0 && !options.quiet) {
    FS_TRACE_WARN("minfs: filesystem not unmounted cleanly.\n");
  }

  // Replay the journal before loading any other structures.
  fs::JournalSuperblock journal_superblock = {};
  if (!options.readonly) {
    status = ReplayJournalReloadSuperblock(bc.get(), &info, &journal_superblock);
    if (status != ZX_OK) {
      return status;
    }
  } else if (!options.quiet) {
    FS_TRACE_WARN("minfs: Not replaying journal\n");
  }
#endif

#ifndef __Fuchsia__
  if (bc->extent_lengths_.size() != 0 && bc->extent_lengths_.size() != kExtentCount) {
    FS_TRACE_ERROR("minfs: invalid number of extents\n");
    return ZX_ERR_INVALID_ARGS;
  }
#endif

  std::unique_ptr<SuperblockManager> sb;
  IntegrityCheck checks = options.repair_filesystem ? IntegrityCheck::kAll : IntegrityCheck::kNone;
#ifdef __Fuchsia__
  block_client::BlockDevice* device = bc->device();
  status = SuperblockManager::Create(device, &info, bc->Maxblk(), checks, &sb);
#else
  status = SuperblockManager::Create(&info, bc->Maxblk(), checks, &sb);
#endif

  if (status != ZX_OK) {
    FS_TRACE_ERROR("Minfs::Create failed to initialize superblock: %d\n", status);
    return status;
  }

  std::unique_ptr<Minfs> fs;
  status = Minfs::ReadInitialBlocks(info, std::move(bc), std::move(sb), options, &fs);
  if (status != ZX_OK) {
    return status;
  }

#ifdef __Fuchsia__
  if (!options.readonly) {
    status = fs->InitializeJournal(std::move(journal_superblock));
    if (status != ZX_OK) {
      FS_TRACE_ERROR("minfs: Cannot initialize journal\n");
      return status;
    }

    if (options.fsck_after_every_transaction) {
      FS_TRACE_ERROR("minfs: Will fsck after every transaction\n");
      fs->journal_->set_write_metadata_callback(
          fit::bind_member(fs.get(), &Minfs::FsckAtEndOfTransaction));
    }
  }

  if (options.repair_filesystem && (info.flags & kMinfsFlagFVM)) {
    // After replaying the journal, it's now safe to repair the FVM slices.
    const size_t kBlocksPerSlice = info.slice_size / info.BlockSize();
    zx_status_t status;
    status = CheckSlices(&info, kBlocksPerSlice, device, /*repair_slices=*/true);
    if (status != ZX_OK) {
      return status;
    }
  }

  if (!options.readonly) {
    // On a read-write filesystem we unset the kMinfsFlagClean flag to indicate that the filesystem
    // may begin receiving modifications.
    //
    // The kMinfsFlagClean flag is reset on orderly shutdown.
    status = fs->UpdateCleanBitAndOldestRevision(/*is_clean=*/false);
    if (status != ZX_OK) {
      return status;
    }

    // After loading the rest of the filesystem, purge any remaining nodes in the unlinked list.
    status = fs->PurgeUnlinked();
    if (status != ZX_OK) {
      FS_TRACE_ERROR("minfs: Cannot purge unlinked list\n");
      return status;
    }

    if (options.readonly_after_initialization) {
      // The filesystem should still be "writable"; we set the dirty bit while
      // purging the unlinked list. Invoking StopWriteback here unsets the dirty bit.
      fs->StopWriteback();
    }
  }

  fs->SetReadonly(options.readonly || options.readonly_after_initialization);

  fs->mount_state_ = {
      .readonly_after_initialization = options.readonly_after_initialization,
      .collect_metrics = options.metrics,
      .verbose = options.verbose,
      .repair_filesystem = options.repair_filesystem,
      .use_journal = true,
  };
#endif  // defined(__Fuchsia__)

  *out = std::move(fs);
  return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t ReplayJournal(Bcache* bc, const Superblock& info, fs::JournalSuperblock* out) {
  FS_TRACE_INFO("minfs: Replaying journal\n");

  auto superblock_or =
      fs::ReplayJournal(bc, bc, JournalStartBlock(info), JournalBlocks(info), info.BlockSize());
  if (superblock_or.is_error()) {
    FS_TRACE_ERROR("minfs: Failed to replay journal\n");
    return superblock_or.error_value();
  }

  *out = std::move(superblock_or.value());
  FS_TRACE_DEBUG("minfs: Journal replayed\n");
  return ZX_OK;
}

zx_status_t Minfs::InitializeJournal(fs::JournalSuperblock journal_superblock) {
  if (journal_ != nullptr) {
    FS_TRACE_ERROR("minfs: Journal was already initialized.\n");
    return ZX_ERR_ALREADY_EXISTS;
  }

  std::unique_ptr<storage::BlockingRingBuffer> journal_buffer;
  const uint64_t journal_entry_blocks = JournalBlocks(sb_->Info()) - fs::kJournalMetadataBlocks;
  zx_status_t status = storage::BlockingRingBuffer::Create(GetMutableBcache(), journal_entry_blocks,
                                                           sb_->Info().BlockSize(),
                                                           "minfs-journal-buffer", &journal_buffer);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: Cannot create journal buffer\n");
    return status;
  }

  std::unique_ptr<storage::BlockingRingBuffer> writeback_buffer;
  status = storage::BlockingRingBuffer::Create(GetMutableBcache(), WritebackCapacity(),
                                               sb_->Info().BlockSize(), "minfs-writeback-buffer",
                                               &writeback_buffer);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: Cannot create writeback buffer\n");
    return status;
  }

  journal_ = std::make_unique<fs::Journal>(GetMutableBcache(), std::move(journal_superblock),
                                           std::move(journal_buffer), std::move(writeback_buffer),
                                           JournalStartBlock(sb_->Info()), fs::Journal::Options());
  return ZX_OK;
}

zx_status_t CreateBcache(std::unique_ptr<block_client::BlockDevice> device, bool* out_readonly,
                         std::unique_ptr<minfs::Bcache>* out) {
  fuchsia_hardware_block_BlockInfo info;
  zx_status_t status = device->BlockGetInfo(&info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: Coult not access device info: %d\n", status);
    return status;
  }

  *out_readonly = (info.flags & fuchsia_hardware_block_FLAG_READONLY);
  uint64_t device_size = info.block_size * info.block_count;
  if (device_size == 0) {
    FS_TRACE_ERROR("minfs: Invalid device size\n");
    return status;
  }
  uint64_t block_count = device_size / kMinfsBlockSize;

  if (block_count >= std::numeric_limits<uint32_t>::max()) {
    FS_TRACE_ERROR("minfs: Block count overflow\n");
    return ZX_ERR_OUT_OF_RANGE;
  }

  return minfs::Bcache::Create(std::move(device), static_cast<uint32_t>(block_count), out);
}

#endif

zx_status_t Mount(std::unique_ptr<minfs::Bcache> bc, const MountOptions& options,
                  fbl::RefPtr<VnodeMinfs>* root_out) {
  TRACE_DURATION("minfs", "minfs_mount");

  std::unique_ptr<Minfs> fs;
  zx_status_t status = Minfs::Create(std::move(bc), options, &fs);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: failed to create filesystem object %d\n", status);
    return status;
  }

  fbl::RefPtr<VnodeMinfs> vn;
  status = fs->VnodeGet(&vn, kMinfsRootIno);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: cannot find root inode: %d\n", status);
    return status;
  }

  ZX_DEBUG_ASSERT(vn->IsDirectory());

  __UNUSED auto r = fs.release();
  *root_out = std::move(vn);
  return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t MountAndServe(const MountOptions& mount_options, async_dispatcher_t* dispatcher,
                          std::unique_ptr<minfs::Bcache> bcache, zx::channel mount_channel,
                          fbl::Closure on_unmount, ServeLayout serve_layout) {
  TRACE_DURATION("minfs", "MountAndServe");
  minfs::MountOptions options = mount_options;

  fbl::RefPtr<VnodeMinfs> data_root;
  zx_status_t status = Mount(std::move(bcache), options, &data_root);
  if (status != ZX_OK) {
    return status;
  }

  Minfs* vfs = data_root->Vfs();
  vfs->SetMetrics(options.metrics);
  vfs->SetUnmountCallback(std::move(on_unmount));
  vfs->SetDispatcher(dispatcher);

  fbl::RefPtr<fs::Vnode> export_root;
  switch (serve_layout) {
    case ServeLayout::kDataRootOnly:
      export_root = std::move(data_root);
      break;
    case ServeLayout::kExportDirectory:
      auto outgoing = fbl::MakeRefCounted<fs::PseudoDir>();
      outgoing->AddEntry("root", std::move(data_root));
      export_root = std::move(outgoing);
      break;
  }

  return vfs->ServeDirectory(std::move(export_root), std::move(mount_channel));
}

void Minfs::Shutdown(fs::Vfs::ShutdownCallback cb) {
  // On a read-write filesystem, set the kMinfsFlagClean on a clean unmount.
  FS_TRACE_INFO("minfs: Shutting down\n");
  ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    Sync([this, cb = std::move(cb)](zx_status_t) mutable {
      async::PostTask(dispatcher(), [this, cb = std::move(cb)]() mutable {
        // Ensure writeback buffer completes before auxiliary structures are deleted.
        StopWriteback();

        auto on_unmount = std::move(on_unmount_);

        // Explicitly delete this (rather than just letting the memory release when
        // the process exits) to ensure that the block device's fifo has been
        // closed.
        delete this;

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
#endif

uint32_t BlocksRequiredForInode(uint64_t inode_count) {
  return safemath::checked_cast<uint32_t>((inode_count + kMinfsInodesPerBlock - 1) /
                                          kMinfsInodesPerBlock);
}

uint32_t BlocksRequiredForBits(uint64_t bit_count) {
  return safemath::checked_cast<uint32_t>((bit_count + kMinfsBlockBits - 1) / kMinfsBlockBits);
}

zx_status_t Mkfs(const MountOptions& options, Bcache* bc) {
  Superblock info;
  memset(&info, 0x00, sizeof(info));
  info.magic0 = kMinfsMagic0;
  info.magic1 = kMinfsMagic1;
  info.format_version = kMinfsCurrentFormatVersion;
  info.flags = kMinfsFlagClean;
  info.block_size = kMinfsBlockSize;
  info.inode_size = kMinfsInodeSize;

  uint32_t blocks = 0;
  uint32_t inodes = 0;

  zx_status_t status;
#ifdef __Fuchsia__
  auto fvm_cleanup =
      fbl::MakeAutoCall([device = bc->device(), &info]() { FreeSlices(&info, device); });
  status = CreateFvmData(options, &info, bc->device());
  if (status != ZX_OK) {
    return status;
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
        FS_TRACE_ERROR("mkfs: Partition size (%" PRIu64 " bytes) is too small\n",
                       static_cast<uint64_t>(blocks) * info.BlockSize());
        return ZX_ERR_INVALID_ARGS;
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
  info.oldest_revision = kMinfsCurrentRevision;
  DumpInfo(info);

  RawBitmap abm;
  RawBitmap ibm;

  // By allocating the bitmap and then shrinking it, we keep the underlying
  // storage a block multiple but ensure we can't allocate beyond the last
  // real block or inode.
  if ((status = abm.Reset(fbl::round_up(info.block_count, kMinfsBlockBits))) != ZX_OK) {
    FS_TRACE_ERROR("mkfs: Failed to allocate block bitmap: %d\n", status);
    return status;
  }
  if ((status = ibm.Reset(fbl::round_up(info.inode_count, kMinfsBlockBits))) != ZX_OK) {
    FS_TRACE_ERROR("mkfs: Failed to allocate inode bitmap: %d\n", status);
    return status;
  }
  if ((status = abm.Shrink(info.block_count)) != ZX_OK) {
    FS_TRACE_ERROR("mkfs: Failed to shrink block bitmap: %d\n", status);
    return status;
  }
  if ((status = ibm.Shrink(info.inode_count)) != ZX_OK) {
    FS_TRACE_ERROR("mkfs: Failed to shrink inode bitmap: %d\n", status);
    return status;
  }

  // Write rootdir
  uint8_t blk[info.BlockSize()];
  memset(blk, 0, sizeof(blk));
  InitializeDirectory(blk, kMinfsRootIno, kMinfsRootIno);
  if ((status = bc->Writeblk(info.dat_block + 1, blk)) != ZX_OK) {
    FS_TRACE_ERROR("mkfs: Failed to write root directory: %d\n", status);
    return status;
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
    if ((status = bc->Writeblk(info.abm_block + n, blk)) != ZX_OK) {
      return status;
    }
  }

  // Write inode bitmap
  for (uint32_t n = 0; n < ibmblks; n++) {
    void* bmdata = fs::GetBlock(info.BlockSize(), ibm.StorageUnsafe()->GetData(), n);
    memcpy(blk, bmdata, info.BlockSize());
    if ((status = bc->Writeblk(info.ibm_block + n, blk)) != ZX_OK) {
      return status;
    }
  }

  // Write inodes
  memset(blk, 0, sizeof(blk));
  for (uint32_t n = 0; n < inoblks; n++) {
    if ((status = bc->Writeblk(info.ino_block + n, blk)) != ZX_OK) {
      return status;
    }
  }

  // Setup root inode
  Inode* ino = reinterpret_cast<Inode*>(&blk[0]);
  ino[kMinfsRootIno].magic = kMinfsMagicDir;
  ino[kMinfsRootIno].size = info.BlockSize();
  ino[kMinfsRootIno].block_count = 1;
  ino[kMinfsRootIno].link_count = 2;
  ino[kMinfsRootIno].dirent_count = 2;
  ino[kMinfsRootIno].dnum[0] = 1;
  ino[kMinfsRootIno].create_time = GetTimeUTC();
  bc->Writeblk(info.ino_block, blk);

  info.generation_count = 0;
  UpdateChecksum(&info);

  // Write superblock info to disk.
  bc->Writeblk(kSuperblockStart, &info);

  // Write backup superblock info to disk.
  if ((info.flags & kMinfsFlagFVM) == 0) {
    bc->Writeblk(kNonFvmSuperblockBackup, &info);
  } else {
    bc->Writeblk(kFvmSuperblockBackup, &info);
  }

  fs::WriteBlocksFn write_blocks_fn = [bc, info](fbl::Span<const uint8_t> buffer,
                                                 uint64_t block_offset, uint64_t block_count) {
    ZX_ASSERT((block_count + block_offset) <= JournalBlocks(info));
    ZX_ASSERT(buffer.size() >= (block_count * info.BlockSize()));
    auto data = buffer.data();
    while (block_count > 0) {
      auto status = bc->Writeblk(static_cast<blk_t>(JournalStartBlock(info) + block_offset), data);
      if (status != ZX_OK) {
        return status;
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

zx_status_t Minfs::ReadDat(blk_t bno, void* data) {
#ifdef __Fuchsia__
  return bc_->Readblk(Info().dat_block + bno, data);
#else
  return ReadBlk(bno, offsets_.DatStartBlock(), offsets_.DatBlockCount(), Info().block_count, data);
#endif
}

zx_status_t Minfs::ReadBlock(blk_t start_block_num, void* out_data) const {
  return bc_->Readblk(start_block_num, out_data);
}

#ifndef __Fuchsia__
zx_status_t Minfs::ReadBlk(blk_t bno, blk_t start, blk_t soft_max, blk_t hard_max,
                           void* data) const {
  if (bno >= hard_max) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (bno >= soft_max) {
    memset(data, 0, BlockSize());
    return ZX_OK;
  }

  return bc_->Readblk(start + bno, data);
}

zx_status_t CreateBcacheFromFd(fbl::unique_fd fd, off_t start, off_t end,
                               const fbl::Vector<size_t>& extent_lengths,
                               std::unique_ptr<minfs::Bcache>* out) {
  if (start >= end) {
    fprintf(stderr, "error: Insufficient space allocated\n");
    return ZX_ERR_INVALID_ARGS;
  }

  if (extent_lengths.size() != kExtentCount) {
    FS_TRACE_ERROR("error: invalid number of extents : %lu\n", extent_lengths.size());
    return ZX_ERR_INVALID_ARGS;
  }

  struct stat s;
  if (fstat(fd.get(), &s) < 0) {
    FS_TRACE_ERROR("error: minfs could not find end of file/device\n");
    return ZX_ERR_IO;
  }

  if (s.st_size < end) {
    FS_TRACE_ERROR("error: invalid file size\n");
    return ZX_ERR_INVALID_ARGS;
  }

  size_t size = (end - start) / minfs::kMinfsBlockSize;

  std::unique_ptr<minfs::Bcache> bc;
  zx_status_t status = minfs::Bcache::Create(std::move(fd), static_cast<uint32_t>(size), &bc);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("error: cannot create block cache: %d\n", status);
    return status;
  }

  if ((status = bc->SetSparse(start, extent_lengths)) != ZX_OK) {
    FS_TRACE_ERROR("Bcache is already sparse: %d\n", status);
    return status;
  }

  *out = std::move(bc);
  return ZX_OK;
}

zx_status_t SparseFsck(fbl::unique_fd fd, off_t start, off_t end,
                       const fbl::Vector<size_t>& extent_lengths) {
  std::unique_ptr<minfs::Bcache> bc;
  zx_status_t status;
  if ((status = CreateBcacheFromFd(std::move(fd), start, end, extent_lengths, &bc)) != ZX_OK) {
    return status;
  }

  return Fsck(std::move(bc), FsckOptions());
}

zx_status_t SparseUsedDataSize(fbl::unique_fd fd, off_t start, off_t end,
                               const fbl::Vector<size_t>& extent_lengths, uint64_t* out_size) {
  std::unique_ptr<minfs::Bcache> bc;
  zx_status_t status;

  if ((status = CreateBcacheFromFd(std::move(fd), start, end, extent_lengths, &bc)) != ZX_OK) {
    return status;
  }
  return UsedDataSize(bc, out_size);
}

zx_status_t SparseUsedInodes(fbl::unique_fd fd, off_t start, off_t end,
                             const fbl::Vector<size_t>& extent_lengths, uint64_t* out_inodes) {
  std::unique_ptr<minfs::Bcache> bc;
  zx_status_t status;
  if ((status = CreateBcacheFromFd(std::move(fd), start, end, extent_lengths, &bc)) != ZX_OK) {
    return status;
  }
  return UsedInodes(bc, out_inodes);
}

zx_status_t SparseUsedSize(fbl::unique_fd fd, off_t start, off_t end,
                           const fbl::Vector<size_t>& extent_lengths, uint64_t* out_size) {
  std::unique_ptr<minfs::Bcache> bc;
  zx_status_t status;

  if ((status = CreateBcacheFromFd(std::move(fd), start, end, extent_lengths, &bc)) != ZX_OK) {
    return status;
  }
  return UsedSize(bc, out_size);
}

#endif

#ifdef __Fuchsia__
fbl::Vector<BlockRegion> Minfs::GetAllocatedRegions() const {
  return block_allocator_->GetAllocatedRegions();
}
#endif

}  // namespace minfs
