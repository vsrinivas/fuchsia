// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <utility>

#ifdef __Fuchsia__
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>
#endif

#include <fbl/algorithm.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/queue.h>
#include <fs/vfs.h>
#include <minfs/allocator-promise.h>
#include <minfs/bcache.h>
#include <minfs/block-txn.h>
#include <minfs/format.h>

namespace minfs {

class VnodeMinfs;

// A wrapper around a WriteTxn, holding references to the underlying Vnodes
// corresponding to the txn, so their Vnodes (and VMOs) are not released
// while being written out to disk.
//
// Additionally, this class allows completions to be signalled when the transaction
// has successfully completed.
class WritebackWork : public WriteTxn,
                      public fbl::SinglyLinkedListable<fbl::unique_ptr<WritebackWork>> {
public:
    WritebackWork(Bcache* bc);

    // Sets the WritebackWork to a completed state. |status| should indicate whether the work was
    // completed successfully.
    void MarkCompleted(zx_status_t status);

    // Allow "pinning" Vnodes so they aren't destroyed while we're completing
    // this writeback operation.
    void PinVnode(fbl::RefPtr<VnodeMinfs> vn);

    // Actually transacts the enqueued work, and resets the WritebackWork to
    // its initial state. Returns the result of the transaction.
    zx_status_t Complete();

#ifdef __Fuchsia__
    // Adds a closure to the WritebackWork, such that it will be signalled
    // when the WritebackWork is flushed to disk.
    // If no closure is set, nothing will get signalled.
    //
    // Only one closure may be set for each WritebackWork unit.
    using SyncCallback = fs::Vnode::SyncCallback;
    void SetSyncCallback(SyncCallback closure);
#endif
private:
#ifdef __Fuchsia__
    // If a sync callback exists, call it with |status| and delete it.
    // Also delete any other existing callbacks.
    void ResetCallbacks(zx_status_t status);

    SyncCallback sync_cb_; // Optional.
#endif
    size_t node_count_;
    // May be empty. Currently '4' is the maximum number of vnodes within a
    // single unit of writeback work, which occurs during a cross-directory
    // rename operation.
    fbl::RefPtr<VnodeMinfs> vn_[4];
};

// Tracks the current transaction, including any enqueued writes, and reserved blocks
// and inodes. Also handles allocation of previously reserved blocks/inodes.
class Transaction {
public:
    Transaction(fbl::unique_ptr<WritebackWork> work,
                fbl::unique_ptr<AllocatorPromise> inode_promise,
                fbl::unique_ptr<AllocatorPromise> block_promise)
        : work_(std::move(work)),
          inode_promise_(std::move(inode_promise)),
          block_promise_(std::move(block_promise)) {}

    size_t AllocateInode() {
        ZX_DEBUG_ASSERT(inode_promise_ != nullptr);
        return inode_promise_->Allocate(GetWork());
    }

    size_t AllocateBlock() {
        ZX_DEBUG_ASSERT(block_promise_ != nullptr);
        return block_promise_->Allocate(GetWork());
    }

#ifdef __Fuchsia__
    size_t SwapBlock(size_t old_bno) {
        ZX_DEBUG_ASSERT(block_promise_ != nullptr);
        return block_promise_->Swap(old_bno);
    }

    void Resolve() {
        if (block_promise_->IsInitialized()) {
            block_promise_->SwapCommit(GetWork());
        }
    }
#endif

    void SetWork(fbl::unique_ptr<WritebackWork> work) {
        work_ = std::move(work);
    }

    WritebackWork* GetWork() {
        ZX_DEBUG_ASSERT(work_ != nullptr);
        return work_.get();
    }

    fbl::unique_ptr<WritebackWork> RemoveWork() {
        ZX_DEBUG_ASSERT(work_ != nullptr);
        return std::move(work_);
    }

private:
    fbl::unique_ptr<WritebackWork> work_;
    fbl::unique_ptr<AllocatorPromise> inode_promise_;
    fbl::unique_ptr<AllocatorPromise> block_promise_;
};

} // namespace minfs
