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
// Additionally, it is responsible for allocating and freeing inodes.
class InodeManager {
public:
    InodeManager() = delete;
    DISALLOW_COPY_ASSIGN_AND_MOVE(InodeManager);
    ~InodeManager();

    static zx_status_t Create(Bcache* bc, Superblock* sb, ReadTxn* txn,
                              AllocatorMetadata metadata,
                              blk_t start_block, size_t inodes,
                              fbl::unique_ptr<InodeManager>* out);

    // Allocate a new inode.
    zx_status_t Allocate(WriteTxn* txn, size_t* out_index) {
        return inode_allocator_->Allocate(txn, out_index);
    }

    // Free an inode.
    void Free(WriteTxn* txn, size_t index) {
        inode_allocator_->Free(txn, index);
    }

    // Persist the inode to storage.
    void Update(WriteTxn* txn, ino_t ino, const minfs_inode_t* inode);

    // Load the inode from storage.
    void Load(ino_t ino, minfs_inode_t* out) const;

    // Extend the number of inodes managed.
    //
    // It is the caller's responsibility to ensure that there is space
    // on persistent storage for these inodes to be stored.
    zx_status_t Grow(size_t inodes);

private:
    friend class MinfsChecker;

    InodeManager(Bcache* bc, blk_t start_block);

    Bcache* bc_;
    blk_t start_block_;
    fbl::unique_ptr<Allocator> inode_allocator_;
#ifdef __Fuchsia__
    fbl::unique_ptr<fs::MappedVmo> inode_table_{};
#endif
};

} // namespace minfs
