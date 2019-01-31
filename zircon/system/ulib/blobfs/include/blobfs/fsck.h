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
    BlobfsChecker();
    void Init(fbl::unique_ptr<Blobfs> vnode);
    void TraverseInodeBitmap();
    void TraverseBlockBitmap();
    zx_status_t CheckAllocatedCounts() const;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(BlobfsChecker);
    fbl::unique_ptr<Blobfs> blobfs_;
    uint32_t alloc_inodes_;
    uint32_t alloc_blocks_;
    uint32_t error_blobs_;
    uint32_t inode_blocks_;
};

zx_status_t Fsck(fbl::unique_ptr<Blobfs> vnode);

} // namespace blobfs
