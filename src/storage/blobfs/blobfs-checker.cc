// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs-checker.h"

#include <inttypes.h>

#include <iterator>
#include <utility>

#include <fs/trace.h>

#ifdef __Fuchsia__

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <zircon/status.h>

#include <fs/journal/replay.h>

#else

#include <blobfs/host.h>

#endif

#include "iterator/allocated-extent-iterator.h"
#include "iterator/extent-iterator.h"

namespace blobfs {

void BlobfsChecker::TraverseInodeBitmap() {
  for (unsigned n = 0; n < blobfs_->info_.inode_count; n++) {
    InodePtr inode = blobfs_->GetNode(n);
    if (inode->header.IsAllocated()) {
      alloc_inodes_++;
      if (inode->header.IsExtentContainer()) {
        // TODO(smklein): sanity check these containers.
        continue;
      }

      bool valid = true;

      AllocatedExtentIterator extents = AllocatedExtentIterator(blobfs_->GetNodeFinder(), n);
      while (!extents.Done()) {
        const Extent* extent;
        zx_status_t status = extents.Next(&extent);
        if (status != ZX_OK) {
          FS_TRACE_ERROR("check: Failed to acquire extent %u within inode %u.\n",
                         extents.ExtentIndex(), n);
          valid = false;
          break;
        }

        uint64_t start_block = extent->Start();
        uint64_t end_block = extent->Start() + extent->Length();
        uint64_t first_unset = 0;
        if (!blobfs_->CheckBlocksAllocated(start_block, end_block, &first_unset)) {
          FS_TRACE_ERROR("check: ino %u using blocks [%" PRIu64 ", %" PRIu64
                         "). "
                         "Not fully allocated in block bitmap; first unset @%" PRIu64 "\n",
                         n, start_block, end_block, first_unset);
          valid = false;
        }
        inode_blocks_ += extent->Length();
      }

      if (blobfs_->LoadAndVerifyBlob(n) != ZX_OK) {
        FS_TRACE_ERROR("check: detected inode %u with bad state\n", n);
        valid = false;
      }
      if (!valid) {
        error_blobs_++;
      }
    }
  }
}

void BlobfsChecker::TraverseBlockBitmap() {
  for (uint64_t n = 0; n < blobfs_->info_.data_block_count; n++) {
    if (blobfs_->CheckBlocksAllocated(n, n + 1)) {
      alloc_blocks_++;
    }
  }
}

zx_status_t BlobfsChecker::CheckAllocatedCounts() const {
  zx_status_t status = ZX_OK;
  if (alloc_blocks_ != blobfs_->info_.alloc_block_count) {
    FS_TRACE_ERROR("check: incorrect allocated block count %" PRIu64 " (should be %u)\n",
                   blobfs_->info_.alloc_block_count, alloc_blocks_);
    status = ZX_ERR_BAD_STATE;
  }

  if (alloc_blocks_ < kStartBlockMinimum) {
    FS_TRACE_ERROR("check: allocated blocks (%u) are less than minimum (%" PRIu64 ")\n",
                   alloc_blocks_, kStartBlockMinimum);
    status = ZX_ERR_BAD_STATE;
  }

  if (inode_blocks_ + kStartBlockMinimum != alloc_blocks_) {
    FS_TRACE_ERROR(
        "check: bitmap allocated blocks (%u) do not match inode allocated blocks "
        "(%" PRIu64 ")\n",
        alloc_blocks_, inode_blocks_ + kStartBlockMinimum);
    status = ZX_ERR_BAD_STATE;
  }

  if (alloc_inodes_ != blobfs_->info_.alloc_inode_count) {
    FS_TRACE_ERROR("check: incorrect allocated inode count %" PRIu64 " (should be %u)\n",
                   blobfs_->info_.alloc_inode_count, alloc_inodes_);
    status = ZX_ERR_BAD_STATE;
  }

  if (error_blobs_) {
    status = ZX_ERR_BAD_STATE;
  }

  return status;
}

zx_status_t BlobfsChecker::Check() {
  TraverseInodeBitmap();
  TraverseBlockBitmap();
  return CheckAllocatedCounts();
}

BlobfsChecker::BlobfsChecker(std::unique_ptr<Blobfs> blobfs, Options options)
    : blobfs_(std::move(blobfs)), options_(options) {}

zx_status_t BlobfsChecker::Initialize(bool apply_journal) {
#ifdef __Fuchsia__
  zx_status_t status;
  if (apply_journal) {
    status = fs::ReplayJournal(blobfs_.get(), blobfs_.get(), JournalStartBlock(blobfs_->info_),
                               JournalBlocks(blobfs_->info_), kBlobfsBlockSize, nullptr);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("blobfs: Unable to apply journal contents: %d\n", status);
      return status;
    }
  }

  status = CheckFvmConsistency(&blobfs_->Info(), blobfs_->Device(), options_.repair);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Inconsistent metadata does not match FVM: %d\n", status);
    return status;
  }
#endif
  return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t CheckFvmConsistency(const Superblock* info, BlockDevice* device, bool repair) {
  if ((info->flags & kBlobFlagFVM) == 0) {
    return ZX_OK;
  }

  fuchsia_hardware_block_volume_VolumeInfo fvm_info;
  zx_status_t status = device->VolumeQuery(&fvm_info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Unable to query FVM, status: %s\n", zx_status_get_string(status));
    return status;
  }

  if (info->slice_size != fvm_info.slice_size) {
    FS_TRACE_ERROR("blobfs: Slice size did not match expected\n");
    return ZX_ERR_BAD_STATE;
  }
  const size_t kBlocksPerSlice = info->slice_size / kBlobfsBlockSize;

  size_t expected_count[4];
  expected_count[0] = info->abm_slices;
  expected_count[1] = info->ino_slices;
  expected_count[2] = info->journal_slices;
  expected_count[3] = info->dat_slices;

  uint64_t start_slices[4];
  start_slices[0] = kFVMBlockMapStart / kBlocksPerSlice;
  start_slices[1] = kFVMNodeMapStart / kBlocksPerSlice;
  start_slices[2] = kFVMJournalStart / kBlocksPerSlice;
  start_slices[3] = kFVMDataStart / kBlocksPerSlice;

  fuchsia_hardware_block_volume_VsliceRange
      ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
  size_t actual_ranges_count;
  status = device->VolumeQuerySlices(start_slices, std::size(start_slices), ranges,
                                     &actual_ranges_count);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot query slices, status: %s\n", zx_status_get_string(status));
    return status;
  }

  if (actual_ranges_count != std::size(start_slices)) {
    FS_TRACE_ERROR("blobfs: Missing slice\n");
    return ZX_ERR_BAD_STATE;
  }

  for (size_t i = 0; i < std::size(start_slices); i++) {
    size_t blobfs_count = expected_count[i];
    size_t fvm_count = ranges[i].count;

    if (!ranges[i].allocated || fvm_count < blobfs_count) {
      // Currently, since Blobfs can only grow new slices, it should not be possible for
      // the FVM to report a slice size smaller than what is reported by Blobfs. In this
      // case, automatically fail without trying to resolve the situation, as it is
      // possible that Blobfs structures are allocated in the slices that have been lost.
      FS_TRACE_ERROR("blobfs: Mismatched slice count\n");
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    if (fvm_count > blobfs_count && repair) {
      // If FVM reports more slices than we expect, try to free remainder.
      uint64_t offset = start_slices[i] + blobfs_count;
      uint64_t length = fvm_count - blobfs_count;
      zx_status_t status = device->VolumeShrink(offset, length);
      if (status != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Unable to shrink to expected size: %s\n",
                       zx_status_get_string(status));
        return status;
      }
    }
  }

  return ZX_OK;
}
#endif  // ifdef __Fuchsia__

}  // namespace blobfs
