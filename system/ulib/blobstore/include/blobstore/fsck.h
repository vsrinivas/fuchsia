// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functionality for checking the consistency of
// Blobstore.

#pragma once

#include <digest/digest.h>
#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fs/trace.h>

#ifdef __Fuchsia__
#include <blobstore/blobstore.h>
#else
#include <blobstore/host.h>
#endif

namespace blobstore {

class BlobstoreChecker {
public:
    BlobstoreChecker();
    void Init(fbl::RefPtr<Blobstore> vnode);
    void TraverseInodeBitmap();
    void TraverseBlockBitmap();
    zx_status_t CheckAllocatedCounts() const;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(BlobstoreChecker);
    fbl::RefPtr<Blobstore> blobstore_;
    uint32_t alloc_inodes_;
    uint32_t alloc_blocks_;
};

zx_status_t blobstore_check(fbl::RefPtr<Blobstore> vnode);

} // namespace blobstore
