// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobstore-private.h"

//TODO(planders): Add more checks for fsck.
namespace blobstore {

void BlobstoreChecker::TraverseInodeBitmap() {
    for (unsigned n = 0; n < blobstore_->info_.inode_count; n++) {
        blobstore_inode_t* inode = blobstore_->GetNode(n);
        if (inode->start_block >= kStartBlockMinimum) {
            alloc_inodes_++;
        }
    }
}

void BlobstoreChecker::TraverseBlockBitmap() {
    for (uint64_t n = 0; n < blobstore_->info_.block_count; n++) {
        if (blobstore_->block_map_.Get(n, n + 1)) {
            alloc_blocks_++;
        }
    }
}

mx_status_t BlobstoreChecker::CheckAllocatedCounts() const {
    mx_status_t status = MX_OK;
    if (alloc_blocks_ != blobstore_->info_.alloc_block_count) {
        FS_TRACE_ERROR("check: incorrect allocated block count %lu (should be %u)\n", blobstore_->info_.alloc_block_count, alloc_blocks_);
        status = MX_ERR_BAD_STATE;
    }

    if (alloc_inodes_ != blobstore_->info_.alloc_inode_count) {
        FS_TRACE_ERROR("check: incorrect allocated inode count %lu (should be %u)\n", blobstore_->info_.alloc_inode_count, alloc_inodes_);
        status = MX_ERR_BAD_STATE;
    }

    return status;
}

BlobstoreChecker::BlobstoreChecker()
    : blobstore_(nullptr), alloc_inodes_(0), alloc_blocks_(0){};

void BlobstoreChecker::Init(fbl::RefPtr<Blobstore> blob) {
    blobstore_.reset(blob.get());
}

mx_status_t blobstore_check(fbl::RefPtr<Blobstore> blob) {
    mx_status_t status = MX_OK;
    BlobstoreChecker chk;
    chk.Init(fbl::move(blob));
    chk.TraverseInodeBitmap();
    chk.TraverseBlockBitmap();
    status |= (status != MX_OK) ? 0 : chk.CheckAllocatedCounts();
    return status;
}

} // namespace blobstore