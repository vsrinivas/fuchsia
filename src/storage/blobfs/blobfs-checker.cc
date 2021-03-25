// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blobfs-checker.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>

#include <iterator>
#include <utility>

#ifdef __Fuchsia__

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <zircon/status.h>

#else

#include "src/storage/blobfs/host.h"

#endif

#include "src/storage/blobfs/iterator/allocated-extent-iterator.h"
#include "src/storage/blobfs/iterator/extent-iterator.h"

namespace blobfs {

bool BlobfsChecker::CheckBackupSuperblock() {
  if ((blobfs_->Info().flags & kBlobFlagFVM) == 0 ||
      blobfs_->Info().oldest_revision < kBlobfsRevisionBackupSuperblock)
    return true;
  auto superblock_or = blobfs_->ReadBackupSuperblock();
  if (superblock_or.is_error()) {
    FX_LOGS(ERROR) << "could not read backup superblock: " << superblock_or.status_value();
    return false;
  }
  if (zx_status_t status =
          CheckSuperblock(superblock_or.value().get(), TotalBlocks(*superblock_or.value()));
      status != ZX_OK) {
    FX_LOGS(ERROR) << "bad backup superblock: " << status;
    return false;
  }
  return true;
}

bool BlobfsChecker::TraverseInodeBitmap() {
  bool valid = true;
  for (unsigned n = 0; n < blobfs_->info_.inode_count; n++) {
    auto inode = blobfs_->GetNode(n);
    ZX_ASSERT_MSG(inode.is_ok(), "Failed to get node %u: status=%d", n, inode.status_value());
    if (inode->header.IsAllocated()) {
      alloc_inodes_++;
      if (inode->header.IsExtentContainer()) {
        // Extent containers need no validation since we're going to validate the data of all blobs.
        continue;
      }

      bool blob_valid = true;

      auto extents = AllocatedExtentIterator::Create(blobfs_->GetNodeFinder(), n);
      ZX_ASSERT_MSG(extents.is_ok(), "Failed to create extent iterator for inode %u: status=%d", n,
                    extents.status_value());

      while (!extents->Done()) {
        const Extent* extent;
        zx_status_t status = extents->Next(&extent);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "check: Failed to acquire extent " << extents->ExtentIndex()
                         << " within inode " << n << ": " << *inode.value();
          blob_valid = false;
          break;
        }
        if (extent->Length() == 0) {
          FX_LOGS(ERROR) << "check: Found zero-length extent at idx " << extents->ExtentIndex()
                         << " within inode " << n << ": " << *inode.value();
          blob_valid = false;
          break;
        }

        uint64_t start_block = extent->Start();
        uint64_t end_block = extent->Start() + extent->Length();
        uint64_t first_unset = 0;
        if (!blobfs_->CheckBlocksAllocated(start_block, end_block, &first_unset)) {
          FX_LOGS(ERROR) << "check: ino " << n << " using blocks [" << start_block << ", "
                         << end_block
                         << "). "
                            "Not fully allocated in block bitmap; first unset @"
                         << first_unset;
          blob_valid = false;
        }
        inode_blocks_ += extent->Length();
      }

      if (blob_valid) {
        if (zx_status_t status = blobfs_->LoadAndVerifyBlob(n); status != ZX_OK) {
          FX_LOGS(ERROR) << "check: detected inode " << n << " with bad state: " << status;
          blob_valid = false;
        }
      }
      if (!blob_valid) {
        valid = false;
      }
    }
  }
  return valid;
}

bool BlobfsChecker::TraverseBlockBitmap() {
  for (uint64_t n = 0; n < blobfs_->info_.data_block_count; n++) {
    if (blobfs_->CheckBlocksAllocated(n, n + 1)) {
      alloc_blocks_++;
    }
  }
  return true;
}

bool BlobfsChecker::CheckAllocatedCounts() const {
  bool valid = true;
  if (alloc_blocks_ != blobfs_->info_.alloc_block_count) {
    FX_LOGS(ERROR) << "check: incorrect allocated block count " << blobfs_->info_.alloc_block_count
                   << " (should be " << alloc_blocks_ << ")";
    valid = false;
  }

  if (alloc_blocks_ < kStartBlockMinimum) {
    FX_LOGS(ERROR) << "check: allocated blocks (" << alloc_blocks_ << ") are less than minimum ("
                   << kStartBlockMinimum << ")";
    valid = false;
  }

  if (inode_blocks_ + kStartBlockMinimum != alloc_blocks_) {
    FX_LOGS(ERROR) << "check: bitmap allocated blocks (" << alloc_blocks_
                   << ") do not match inode allocated blocks "
                      "("
                   << inode_blocks_ + kStartBlockMinimum << ")";
    valid = false;
  }

  if (alloc_inodes_ != blobfs_->info_.alloc_inode_count) {
    FX_LOGS(ERROR) << "check: incorrect allocated inode count " << blobfs_->info_.alloc_inode_count
                   << " (should be " << alloc_inodes_ << ")";
    valid = false;
  }
  return valid;
}

bool BlobfsChecker::Check() {
  bool valid = true;
  FX_LOGS(INFO) << "Checking backup superblock...";
  valid &= CheckBackupSuperblock();
  FX_LOGS(INFO) << "Verifying inodes and blob data...";
  valid &= TraverseInodeBitmap();
  FX_LOGS(INFO) << "Checking allocation counts...";
  valid &= TraverseBlockBitmap();
  valid &= CheckAllocatedCounts();
  return valid;
}

BlobfsChecker::BlobfsChecker(std::unique_ptr<Blobfs> blobfs, Options options)
    : blobfs_(std::move(blobfs)), options_(options) {}

#ifdef __Fuchsia__
zx_status_t CheckFvmConsistency(const Superblock* info, BlockDevice* device, bool repair) {
  if ((info->flags & kBlobFlagFVM) == 0) {
    return ZX_OK;
  }

  fuchsia_hardware_block_volume_VolumeInfo fvm_info;
  zx_status_t status = device->VolumeQuery(&fvm_info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to query FVM, status: " << zx_status_get_string(status);
    return status;
  }

  if (info->slice_size != fvm_info.slice_size) {
    FX_LOGS(ERROR) << "Slice size did not match expected";
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
    FX_LOGS(ERROR) << "Cannot query slices, status: " << zx_status_get_string(status);
    return status;
  }

  if (actual_ranges_count != std::size(start_slices)) {
    FX_LOGS(ERROR) << "Missing slice";
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
      FX_LOGS(ERROR) << "Mismatched slice count";
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    if (fvm_count > blobfs_count && repair) {
      // If FVM reports more slices than we expect, try to free remainder.
      uint64_t offset = start_slices[i] + blobfs_count;
      uint64_t length = fvm_count - blobfs_count;
      zx_status_t status = device->VolumeShrink(offset, length);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Unable to shrink to expected size: " << zx_status_get_string(status);
        return status;
      }
    }
  }

  return ZX_OK;
}
#endif  // ifdef __Fuchsia__

}  // namespace blobfs
