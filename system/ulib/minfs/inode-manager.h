// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to access inodes.
// Currently, this structure is implemented on-disk as a table.

#pragma once

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>
#include <fs/mapped-vmo.h>

#include <minfs/format.h>
#include <minfs/block-txn.h>

#include "allocator.h"

namespace minfs {

// InodeManager is responsible for owning the persistent storage for inodes.
//
// It can be used to Load and Update inodes on storage.
class InodeManager {
public:
    // The default constructor does not produce a valid InodeManager.
    // Initialize must be called before using any other methods.
    InodeManager();
    ~InodeManager();
    DISALLOW_COPY_ASSIGN_AND_MOVE(InodeManager);
    zx_status_t Initialize(Bcache* bc, ReadTxn* txn, blk_t start_block, size_t inodes);

    // Persiste the inode to storage.
    void Update(WriteTxn* txn, ino_t ino, const minfs_inode_t* inode);

    // Load the inode from storage.
    void Load(ino_t ino, minfs_inode_t* out) const;

    // Extend the number of inodes managed.
    //
    // It is the caller's responsibility to ensure that there is space
    // on persistent storage for these inodes to be stored.
    zx_status_t Grow(size_t inodes);

private:
    blk_t start_block_;
#ifdef __Fuchsia__
    fbl::unique_ptr<fs::MappedVmo> inode_table_{};
#else
    Bcache* bc_;
#endif
};

} // namespace minfs
