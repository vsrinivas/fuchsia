// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functionality for checking the consistency of
// Blobfs.

#pragma once

#ifdef __Fuchsia__
#include <blobfs/blobfs.h>
#else
#include <blobfs/host.h>
#endif

namespace blobfs {

class BlobfsChecker {
public:
    BlobfsChecker(fbl::unique_ptr<Blobfs> blobfs);

    zx_status_t Initialize(bool apply_journal);
    void TraverseInodeBitmap();
    void TraverseBlockBitmap();
    zx_status_t CheckAllocatedCounts() const;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(BlobfsChecker);
    fbl::unique_ptr<Blobfs> blobfs_;
    uint32_t alloc_inodes_ = 0;
    uint32_t alloc_blocks_ = 0;
    uint32_t error_blobs_ = 0;
    uint32_t inode_blocks_ = 0;
};

zx_status_t Fsck(fbl::unique_ptr<Blobfs> vnode, bool apply_jouranl);

#ifdef __Fuchsia__
// Validate that the contents of the superblock matches the results claimed in the underlying
// volume manager.
//
// If the results are inconsistent, update the FVM's allocation accordingly.
zx_status_t CheckFvmConsistency(const Superblock* info, const zx::unowned_channel channel);
#endif // __Fuchsia__

} // namespace blobfs
