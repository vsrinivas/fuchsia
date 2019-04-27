// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/buffer.h>
#include <blobfs/writeback-queue.h>
#include <blobfs/writeback-work.h>
#include <fbl/auto_lock.h>

#include <utility>

namespace blobfs {

WritebackQueue::~WritebackQueue() {
    // Ensure that thread teardown has completed, or that it was never brought up to begin with.
    ZX_DEBUG_ASSERT(!IsRunning());
    ZX_DEBUG_ASSERT(work_queue_.is_empty());
}

zx_status_t WritebackQueue::Teardown() {
    WritebackState state;

    {
        fbl::AutoLock lock(&lock_);
        state = state_;

        // Signal the background thread.
        unmounting_ = true;
        cnd_signal(&work_added_);
    }

    zx_status_t status = ZX_OK;
    if (state != WritebackState::kInit) {
        // Block until the thread completes itself.
        int result = -1;
        int success = thrd_join(worker_, &result);
        if (result != 0 || success != thrd_success) {
            status = ZX_ERR_INTERNAL;
        }
    }

    return status;
}

zx_status_t WritebackQueue::Create(TransactionManager* transaction_manager,
                                   const size_t buffer_blocks,
                                   fbl::unique_ptr<WritebackQueue>* out) {
    zx_status_t status;
    fbl::unique_ptr<Buffer> buffer;
    if ((status = Buffer::Create(transaction_manager, buffer_blocks, "blobfs-writeback", &buffer))
        != ZX_OK) {
        return status;
    }

    fbl::unique_ptr<WritebackQueue> wb(new WritebackQueue(std::move(buffer)));

    if (cnd_init(&wb->work_completed_) != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    }
    if (cnd_init(&wb->work_added_) != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    }

    if (thrd_create_with_name(&wb->worker_,
                              WritebackQueue::WritebackThread, wb.get(),
                              "blobfs-writeback") != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    }

    fbl::AutoLock lock(&wb->lock_);
    wb->state_ = WritebackState::kRunning;
    *out = std::move(wb);
    return ZX_OK;
}

zx_status_t WritebackQueue::Enqueue(fbl::unique_ptr<WritebackWork> work) {
    TRACE_DURATION("blobfs", "WritebackQueue::Enqueue", "work ptr", work.get());
    fbl::AutoLock lock(&lock_);
    zx_status_t status = ZX_OK;

    if (IsReadOnly()) {
        // If we are in a readonly state, return an error. However, the work should still be
        // enqueued and ultimately processed by the WritebackThread. This will help us avoid
        // potential race conditions if the work callback must acquire a lock.
        status = ZX_ERR_BAD_STATE;
    } else if (!work->Transaction().IsBuffered()) {
        ZX_DEBUG_ASSERT(state_ == WritebackState::kRunning);

        // Only copy blocks to the buffer if they have not already been copied to another buffer.
        EnsureSpaceLocked(work->Transaction().BlkCount());

        // It is possible that the queue entered a read only state
        // while we were waiting to ensure space, so check again now.
        if (IsReadOnly()) {
            status = ZX_ERR_BAD_STATE;
        } else {
            buffer_->CopyTransaction(&work->Transaction());
        }
    }

    work_queue_.push(std::move(work));
    cnd_signal(&work_added_);
    return status;
}

bool WritebackQueue::IsRunning() const {
    switch (state_) {
    case WritebackState::kRunning:
    case WritebackState::kReadOnly:
        return true;
    default:
        return false;
    }
}

void WritebackQueue::EnsureSpaceLocked(size_t blocks) {
    while (!buffer_->IsSpaceAvailable(blocks)) {
        // Not enough room to write back work, yet. Wait until room is available.
        Waiter w;
        producer_queue_.push(&w);

        do {
            cnd_wait(&work_completed_, lock_.GetInternal());
        } while ((&producer_queue_.front() != &w) && // We are first in line to enqueue...
                (!buffer_->IsSpaceAvailable(blocks))); // ... and there is enough space for us.

        producer_queue_.pop();
    }
}

int WritebackQueue::WritebackThread(void* arg) {
    WritebackQueue* b = reinterpret_cast<WritebackQueue*>(arg);

    b->lock_.Acquire();
    while (true) {
        bool error = b->IsReadOnly();
        while (!b->work_queue_.is_empty()) {
            if (!error && !b->work_queue_.front().IsReady()) {
                // If the work is not yet ready, break and wait until we receive another signal.
                break;
            }

            auto work = b->work_queue_.pop();
            TRACE_DURATION("blobfs", "WritebackQueue::WritebackThread", "work ptr", work.get());

            bool our_buffer = b->buffer_->VerifyTransaction(&work->Transaction());
            size_t blk_count = work->Transaction().BlkCount();

            // Stay unlocked while processing a unit of work.
            b->lock_.Release();

            if (error) {
                // If we are in a read only state, mark the work complete with an error status.
                work->MarkCompleted(ZX_ERR_BAD_STATE);
            } else {
                // If we should complete the work, make sure it has been buffered.
                // (This is not necessary if we are currently in an error state).
                ZX_DEBUG_ASSERT(work->Transaction().IsBuffered());
                zx_status_t status;
                if ((status = work->Complete()) != ZX_OK) {
                    FS_TRACE_ERROR("Work failed with status %d - "
                                   "converting writeback to read only state.\n", status);
                    // If work completion failed, set the buffer to an error state.
                    error = true;
                }
            }

            work = nullptr;
            b->lock_.Acquire();

            if (error) {
                // If we encountered an error, set the queue to readonly.
                b->state_ = WritebackState::kReadOnly;
            }

            if (our_buffer) {
                // If the last work we processed belonged to our buffer,
                // update the buffer's start/len accordingly.
                b->buffer_->FreeSpace(blk_count);
            }

            // We may have opened up space (or entered a read only state),
            // so signal the producer queue.
            cnd_signal(&b->work_completed_);
        }

        // Before waiting, we should check if we're unmounting.
        // If work still remains in the work or producer queues,
        // continue the loop until they are empty.
        if (b->unmounting_ && b->work_queue_.is_empty() && b->producer_queue_.is_empty()) {
            ZX_DEBUG_ASSERT(b->work_queue_.is_empty());
            ZX_DEBUG_ASSERT(b->producer_queue_.is_empty());
            b->state_ = WritebackState::kComplete;
            b->lock_.Release();
            return 0;
        }

        cnd_wait(&b->work_added_, b->lock_.GetInternal());
    }
}

} // namespace blobfs
