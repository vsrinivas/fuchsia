// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to access inodes.
// Currently, this structure is implemented on-disk as a table.

#pragma once

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>

#ifdef __Fuchsia__
#include <lib/fzl/resizeable-vmo-mapper.h>
#endif

#include <minfs/block-txn.h>
#include <minfs/format.h>

#include "allocator.h"

namespace minfs {

class InspectableInodeManager {
public:
    virtual ~InspectableInodeManager() {}

    // Gets immutable reference to the inode allocator.
    virtual const Allocator* GetInodeAllocator() const = 0;

    // Loads the inode from storage.
    virtual void Load(ino_t inode_num, Inode* out) const = 0;
};

// InodeManager is responsible for owning the persistent storage for inodes.
//
// It can be used to Load and Update inodes on storage.
// Additionally, it is responsible for allocating and freeing inodes.
class InodeManager : public InspectableInodeManager {
public:
    InodeManager() = delete;
    DISALLOW_COPY_ASSIGN_AND_MOVE(InodeManager);
    ~InodeManager();

    static zx_status_t Create(Bcache* bc, SuperblockManager* sb, fs::ReadTxn* txn,
                              AllocatorMetadata metadata,
                              blk_t start_block, size_t inodes,
                              fbl::unique_ptr<InodeManager>* out);

    // Reserve |inodes| inodes in the allocator.
    zx_status_t Reserve(WriteTxn* txn, size_t inodes, AllocatorPromise* promise) {
        return promise->Initialize(txn, inodes, inode_allocator_.get());
    }

    // Free an inode.
    void Free(WriteTxn* txn, size_t index) {
        inode_allocator_->Free(txn, index);
    }

    // Persist the inode to storage.
    void Update(WriteTxn* txn, ino_t ino, const Inode* inode);

    // InspectableInodeManager interface:
    const Allocator* GetInodeAllocator() const final;

    void Load(ino_t ino, Inode* out) const final;

    // Extend the number of inodes managed.
    //
    // It is the caller's responsibility to ensure that there is space
    // on persistent storage for these inodes to be stored.
    zx_status_t Grow(size_t inodes);

private:
    InodeManager(Bcache* bc, blk_t start_block);
#ifndef __Fuchsia__
    Bcache* bc_;
#endif
    blk_t start_block_;
    fbl::unique_ptr<Allocator> inode_allocator_;
#ifdef __Fuchsia__
    fzl::ResizeableVmoMapper inode_table_;
#endif
};

} // namespace minfs
