// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <utility>

#include <blobfs/buffer.h>
#include <blobfs/transaction-manager.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>
#include <fs/block-txn.h>
#include <fs/queue.h>

namespace blobfs {

enum class WritebackState {
    kInit,     // Initial state of a writeback queue.
    kReady,    // Indicates the queue is ready to start running.
    kRunning,  // Indicates that the queue's async processor is currently running.
    kReadOnly, // State of a writeback queue which no longer allows writes.
    kComplete, // Indicates that the async processor has been torn down.
};

// Manages an in-memory writeback buffer (and background thread,
// which flushes this buffer out to disk).
class WritebackQueue {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(WritebackQueue);

    ~WritebackQueue();

    // Initializes the WritebackBuffer at |out|
    // with a buffer of |buffer_blocks| blocks of size kBlobfsBlockSize.
    static zx_status_t Create(TransactionManager* transaction_manager, const size_t buffer_blocks,
                              std::unique_ptr<WritebackQueue>* out);

    // Copies all transaction data referenced from |work| into the writeback buffer.
    zx_status_t Enqueue(std::unique_ptr<WritebackWork> work);

    bool IsReadOnly() const __TA_REQUIRES(lock_) { return state_ == WritebackState::kReadOnly; }

    size_t GetCapacity() const { return buffer_->capacity(); }

    // Stops the asynchronous queue processor. Returns |ZX_ERR_BAD_STATE| if Teardown() has already
    // been called.
    zx_status_t Teardown();

private:
    // The waiter struct may be used as a stack-allocated queue for producers.
    // It allows them to take turns putting data into the buffer when it is
    // mostly full.
    struct Waiter : public fbl::SinglyLinkedListable<Waiter*> {};
    using ProducerQueue = fs::Queue<Waiter*>;
    using WorkQueue = fs::Queue<std::unique_ptr<WritebackWork>>;

    WritebackQueue(std::unique_ptr<Buffer> buffer) : buffer_(std::move(buffer)) {}

    bool IsRunning() const __TA_REQUIRES(lock_);

    // Blocks until |blocks| blocks of data are free for the caller.
    // Doesn't actually allocate any space.
    void EnsureSpaceLocked(size_t blocks) __TA_REQUIRES(lock_);

    // Thread which asynchronously processes transactions.
    static int WritebackThread(void* arg);

    // Signalled when the writeback buffer has space to add txns.
    cnd_t work_completed_;
    // Signalled when the writeback buffer can be consumed by the background thread.
    cnd_t work_added_;

    // Work associated with the "writeback" thread, which manages work items,
    // and flushes them to disk. This thread acts as a consumer of the
    // writeback buffer.
    thrd_t worker_;

    // Use to lock resources that may be accessed asynchronously.
    fbl::Mutex lock_;

    // Buffer which stores transactions to be written out to disk.
    std::unique_ptr<Buffer> buffer_;

    bool unmounting_ __TA_GUARDED(lock_) = false;

    // The WritebackQueue will start off in a kInit state, and will change to kRunning when the
    // background thread is brought up. Once it is running, if an error is detected during
    // writeback, the queue is converted to kReadOnly, and no further writes are permitted.
    WritebackState state_ __TA_GUARDED(lock_) = WritebackState::kInit;

    // Tracks all the pending Writeback Work operations which exist in the
    // writeback buffer and are ready to be sent to disk.
    WorkQueue work_queue_ __TA_GUARDED(lock_){};

    // Ensures that if multiple producers are waiting for space to write their
    // transactions into the writeback buffer, they can each write in-order.
    ProducerQueue producer_queue_ __TA_GUARDED(lock_);
};

} // namespace blobfs
