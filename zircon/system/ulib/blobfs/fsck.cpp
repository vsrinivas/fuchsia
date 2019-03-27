// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/fsck.h>
#include <blobfs/iterator/extent-iterator.h>
#include <fs/trace.h>
#include <inttypes.h>

#ifdef __Fuchsia__

#include <utility>

#include <blobfs/blobfs.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <zircon/status.h>

#else

#include <blobfs/host.h>

#endif

// TODO(planders): Add more checks for fsck.
// TODO(planders): Potentially check the state of the journal.
namespace blobfs {

void BlobfsChecker::TraverseInodeBitmap() {
    for (unsigned n = 0; n < blobfs_->info_.inode_count; n++) {
        Inode* inode = blobfs_->GetNode(n);
        if (inode->header.IsAllocated()) {
            alloc_inodes_++;
            if (inode->header.IsExtentContainer()) {
                // TODO(smklein): sanity check these containers.
                continue;
            }

            bool valid = true;

            AllocatedExtentIterator extents = blobfs_->GetExtents(n);
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
                    FS_TRACE_ERROR("check: ino %u using blocks [%" PRIu64 ", %" PRIu64 "). "
                                   "Not fully allocated in block bitmap; first unset @%" PRIu64 "\n",
                                   n, start_block, end_block, first_unset);
                    valid = false;
                }
                inode_blocks_ += extent->Length();
            }

            if (blobfs_->VerifyBlob(n) != ZX_OK) {
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
        FS_TRACE_ERROR("check: incorrect allocated block count %" PRIu64
                       " (should be %u)\n",
                       blobfs_->info_.alloc_block_count, alloc_blocks_);
        status = ZX_ERR_BAD_STATE;
    }

    if (alloc_blocks_ < kStartBlockMinimum) {
        FS_TRACE_ERROR("check: allocated blocks (%u) are less than minimum%" PRIu64 "\n",
                       alloc_blocks_, kStartBlockMinimum);
        status = ZX_ERR_BAD_STATE;
    }

    if (inode_blocks_ + kStartBlockMinimum != alloc_blocks_) {
        FS_TRACE_ERROR("check: bitmap allocated blocks (%u) do not match inode allocated blocks "
                       "(%" PRIu64 ")\n", alloc_blocks_, inode_blocks_ + kStartBlockMinimum);
        status = ZX_ERR_BAD_STATE;
    }

    if (alloc_inodes_ != blobfs_->info_.alloc_inode_count) {
        FS_TRACE_ERROR("check: incorrect allocated inode count %" PRIu64
                       " (should be %u)\n",
                       blobfs_->info_.alloc_inode_count, alloc_inodes_);
        status = ZX_ERR_BAD_STATE;
    }

    if (error_blobs_) {
        status = ZX_ERR_BAD_STATE;
    }

    return status;
}

BlobfsChecker::BlobfsChecker(fbl::unique_ptr<Blobfs> blobfs)
    : blobfs_(std::move(blobfs)), alloc_inodes_(0), alloc_blocks_(0),
      error_blobs_(0), inode_blocks_(0) {}

zx_status_t BlobfsChecker::Initialize(bool apply_journal) {
#ifdef __Fuchsia__
    // Writability is set to "read-only filesystem", since we may need to replay
    // the journal, but won't need to modify the filesystem further.
    Writability writability = Writability::ReadOnlyFilesystem;
    // Attempt to replay the journal before actually checking the consistency of
    // the filesystem. The "writeback" capabilities are not actually required
    // for this use-case.
    zx_status_t status = blobfs_->InitializeWriteback(writability, apply_journal);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Unable to apply journal contents: %d\n", status);
        return status;
    }

    status = CheckFvmConsistency(&blobfs_->Info(), blobfs_->BlockDevice());
    if (status != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Inconsistent metadata does not match FVM: %d\n", status);
        return status;
    }
#endif
    return ZX_OK;
}

zx_status_t Fsck(fbl::unique_ptr<Blobfs> blob, bool apply_journal) {
    BlobfsChecker checker(std::move(blob));

    // Apply writeback and validate FVM data before walking the contents of the filesystem.
    zx_status_t status = checker.Initialize(apply_journal);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Failed to initialize filesystem; not checking internals\n");
        return status;
    }

    checker.TraverseInodeBitmap();
    checker.TraverseBlockBitmap();
    return checker.CheckAllocatedCounts();
}

#ifdef __Fuchsia__
zx_status_t CheckFvmConsistency(const Superblock* info, const zx::unowned_channel channel) {
    if ((info->flags & kBlobFlagFVM) == 0) {
        return ZX_OK;
    }

    fuchsia_hardware_block_volume_VolumeInfo fvm_info;
    zx_status_t status;
    zx_status_t io_status = fuchsia_hardware_block_volume_VolumeQuery(channel->get(), &status,
                                                                      &fvm_info);
    if (io_status != ZX_OK) {
        status = io_status;
    }
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
    io_status = fuchsia_hardware_block_volume_VolumeQuerySlices(
        channel->get(), start_slices, fbl::count_of(start_slices), &status, ranges,
        &actual_ranges_count);
    if (io_status != ZX_OK) {
        status = io_status;
    }
    if (status != ZX_OK) {
        FS_TRACE_ERROR("blobfs: Cannot query slices, status: %s\n", zx_status_get_string(status));
        return status;
    }

    if (actual_ranges_count != fbl::count_of(start_slices)) {
        FS_TRACE_ERROR("blobfs: Missing slice\n");
        return ZX_ERR_BAD_STATE;
    }

    for (size_t i = 0; i < fbl::count_of(start_slices); i++) {
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

        if (fvm_count > blobfs_count) {
            // If FVM reports more slices than we expect, try to free remainder.
            uint64_t offset = start_slices[i] + blobfs_count;
            uint64_t length = fvm_count - blobfs_count;
            io_status = fuchsia_hardware_block_volume_VolumeShrink(channel->get(), offset, length,
                                                                   &status);
            if (io_status != ZX_OK) {
                status = io_status;
            }
            if (status != ZX_OK) {
                FS_TRACE_ERROR("blobfs: Unable to shrink to expected size: %s\n",
                               zx_status_get_string(status));
                return status;
            }
        }
    }

    return ZX_OK;
}
#endif // ifdef __Fuchsia__

} // namespace blobfs
