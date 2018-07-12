// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __Fuchsia__
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
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
#include <lib/fzl/mapped-vmo.h>

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

    // Return the WritebackWork to the default state that it was in
    // after being created.
    void Reset();

#ifdef __Fuchsia__
    // Actually transacts the enqueued work, and resets the WritebackWork to
    // its initial state.
    //
    // Returns the number of blocks of the writeback buffer that have been
    // consumed.
    size_t Complete(zx_handle_t vmo, vmoid_t vmoid);

    // Adds a closure to the WritebackWork, such that it will be signalled
    // when the WritebackWork is flushed to disk.
    // If no closure is set, nothing will get signalled.
    //
    // Only one closure may be set for each WritebackWork unit.
    using SyncCallback = fs::Vnode::SyncCallback;
    void SetClosure(SyncCallback closure);
#else
    // Flushes any pending transactions.
    void Complete();
#endif

    // Allow "pinning" Vnodes so they aren't destroyed while we're completing
    // this writeback operation.
    void PinVnode(fbl::RefPtr<VnodeMinfs> vn);

private:
#ifdef __Fuchsia__
    SyncCallback closure_; // Optional.
#endif
    size_t node_count_;
    // May be empty. Currently '4' is the maximum number of vnodes within a
    // single unit of writeback work, which occurs during a cross-directory
    // rename operation.
    fbl::RefPtr<VnodeMinfs> vn_[4];
};

#ifdef __Fuchsia__

// WritebackBuffer which manages a writeback buffer (and background thread,
// which flushes this buffer out to disk).
class WritebackBuffer {
public:
    // Calls constructor, return an error if anything goes wrong.
    static zx_status_t Create(Bcache* bc, fbl::unique_ptr<fzl::MappedVmo> buffer,
                              fbl::unique_ptr<WritebackBuffer>* out);
    ~WritebackBuffer();

    // Enqueues work into the writeback buffer.
    // When this function returns, the transaction blocks from |work|
    // have been copied to the writeback buffer, but not necessarily written to
    // disk.
    //
    // To avoid accessing a stale Vnode from disk before the writeback has
    // completed, |work| also contains references to any Vnodes which are
    // enqueued, preventing them from closing while the writeback is pending.
    void Enqueue(fbl::unique_ptr<WritebackWork> work) __TA_EXCLUDES(writeback_lock_);

private:
    WritebackBuffer(Bcache* bc, fbl::unique_ptr<fzl::MappedVmo> buffer);

    // Blocks until |blocks| blocks of data are free for the caller.
    // Returns |ZX_OK| with the lock still held in this case.
    // Returns |ZX_ERR_NO_RESOURCES| if there will never be space for the
    // incoming request (i.e., too many blocks requested).
    //
    // Doesn't actually allocate any space.
    zx_status_t EnsureSpaceLocked(size_t blocks) __TA_REQUIRES(writeback_lock_);

    // Copies a write transaction to the writeback buffer.
    // Also updates the in-memory offsets of the WriteTxn's requests so
    // they point to the correct offsets in the in-memory buffer, not their
    // original VMOs.
    //
    // |EnsureSpaceLocked| should be called before invoking this function to
    // safely guarantee that space exists within the buffer.
    void CopyToBufferLocked(WriteTxn* txn) __TA_REQUIRES(writeback_lock_);

    static int WritebackThread(void* arg);

    // The waiter struct may be used as a stack-allocated queue for producers.
    // It allows them to take turns putting data into the buffer when it is
    // mostly full.
    struct Waiter : public fbl::SinglyLinkedListable<Waiter*> {};
    using WorkQueue = fs::Queue<fbl::unique_ptr<WritebackWork>>;
    using ProducerQueue = fs::Queue<Waiter*>;

    // Signalled when the writeback buffer can be consumed by the background
    // thread.
    cnd_t consumer_cvar_;
    // Signalled when the writeback buffer has space to add txns.
    cnd_t producer_cvar_;

    // Work associated with the "writeback" thread, which manages work items,
    // and flushes them to disk. This thread acts as a consumer of the
    // writeback buffer.
    thrd_t writeback_thrd_;
    Bcache* bc_;
    fbl::Mutex writeback_lock_;

    // Ensures that if multiple producers are waiting for space to write their
    // txns into the writeback buffer, they can each write in-order.
    ProducerQueue producer_queue_ __TA_GUARDED(writeback_lock_){};
    // Tracks all the pending Writeback Work operations which exist in the
    // writeback buffer and are ready to be sent to disk.
    WorkQueue work_queue_ __TA_GUARDED(writeback_lock_){};
    bool unmounting_ __TA_GUARDED(writeback_lock_){false};
    fbl::unique_ptr<fzl::MappedVmo> buffer_{};
    vmoid_t buffer_vmoid_ = VMOID_INVALID;
    // The units of all the following are "MinFS blocks".
    size_t start_ __TA_GUARDED(writeback_lock_){};
    size_t len_ __TA_GUARDED(writeback_lock_){};
    const size_t cap_ = 0;
};

#endif

} // namespace minfs
