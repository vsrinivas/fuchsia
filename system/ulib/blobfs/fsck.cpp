// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/fsck.h>
#include <inttypes.h>

#ifdef __Fuchsia__
#include <blobfs/blobfs.h>
#else
#include <blobfs/host.h>
#endif

//TODO(planders): Add more checks for fsck.
namespace blobfs {

void BlobfsChecker::TraverseInodeBitmap() {
    for (unsigned n = 0; n < blobfs_->info_.inode_count; n++) {
        blobfs_inode_t* inode = blobfs_->GetNode(n);
        if (inode->start_block >= kStartBlockMinimum) {
            alloc_inodes_++;
            inode_blocks_ += static_cast<uint32_t>(inode->num_blocks);

            if (blobfs_->VerifyBlob(n) != ZX_OK) {
                FS_TRACE_ERROR("check: detected inode %u with bad state\n", n);
                error_blobs_++;
            }
        }
    }
}

void BlobfsChecker::TraverseBlockBitmap() {
    for (uint64_t n = 0; n < blobfs_->info_.block_count; n++) {
        if (blobfs_->block_map_.Get(n, n + 1)) {
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
                       "(%u)\n", alloc_blocks_, inode_blocks_);
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

BlobfsChecker::BlobfsChecker()
    : blobfs_(nullptr), alloc_inodes_(0), alloc_blocks_(0), error_blobs_(0), inode_blocks_(0) {};

void BlobfsChecker::Init(fbl::RefPtr<Blobfs> blob) {
    blobfs_.reset(blob.get());
}

zx_status_t blobfs_check(fbl::RefPtr<Blobfs> blob) {
    zx_status_t status = ZX_OK;
    BlobfsChecker chk;
    chk.Init(fbl::move(blob));
    chk.TraverseInodeBitmap();
    chk.TraverseBlockBitmap();
    status |= (status != ZX_OK) ? 0 : chk.CheckAllocatedCounts();
    return status;
}

} // namespace blobfs