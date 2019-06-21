// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <minfs/writeback-async.h>

#include "minfs-private.h"

#include <type_traits>

namespace minfs {

Buffer::~Buffer() {
    if (vmoid_.id != VMOID_INVALID) {
        // Close the buffer vmo.
        block_fifo_request_t request;
        request.group = bc_->BlockGroupID();
        request.vmoid = vmoid_.id;
        request.opcode = BLOCKIO_CLOSE_VMO;
        bc_->Transaction(&request, 1);
    }
}

zx_status_t Buffer::Create(Bcache* bc, blk_t blocks, const char* label,
                           std::unique_ptr<Buffer>* out) {
    zx_status_t status;
    fzl::OwnedVmoMapper mapper;
    if ((status = mapper.CreateAndMap(blocks * kMinfsBlockSize, label)) != ZX_OK) {
        return status;
    }

    std::unique_ptr<Buffer> buffer(new Buffer(bc, std::move(mapper)));

    if ((status = buffer->bc_->AttachVmo(buffer->mapper_.vmo(), &buffer->vmoid_))
        != ZX_OK) {
        fprintf(stderr, "Buffer: Failed to attach vmo\n");
        return status;
    }

    *out = std::move(buffer);
    return ZX_OK;
}

bool Buffer::IsSpaceAvailable(blk_t blocks) const {
    // TODO(planders): Similar to minfs, make sure that we either have a fallback mechanism for
    // operations which are too large to be fully contained by the buffer, or that the
    // worst-case operation will always fit within the buffer.
    ZX_ASSERT_MSG(blocks <= capacity_, "Requested transaction (%u blocks) larger than buffer",
                  blocks);
    return length_ + blocks <= capacity_;
}

void Buffer::CopyTransaction(WriteTxn* write_transaction) {
    ZX_DEBUG_ASSERT(!write_transaction->IsBuffered());
    auto& reqs = write_transaction->Requests();
    blk_t first_block = (start_ + length_) % capacity_;

    for (size_t i = 0; i < reqs.size(); i++) {
        ZX_DEBUG_ASSERT(reqs[i].vmo != ZX_HANDLE_INVALID);

        // Read parameters of the current request.
        blk_t vmo_offset = reqs[i].vmo_offset;
        blk_t dev_offset = reqs[i].dev_offset;
        const blk_t vmo_len = reqs[i].length;
        ZX_DEBUG_ASSERT(vmo_len > 0);

        // Calculate the offset/length we will need to write into the buffer.
        blk_t buf_offset = (start_ + length_) % capacity_;
        blk_t buf_len = (buf_offset + vmo_len > capacity_) ? capacity_ - buf_offset : vmo_len;
        blk_t init_len = vmo_len;
        blk_t total_len = buf_len;

        // Verify that the length is valid.
        ZX_DEBUG_ASSERT(buf_len > 0);
        ZX_DEBUG_ASSERT(buf_len <= vmo_len);
        ZX_DEBUG_ASSERT(buf_len < capacity_);
        zx_handle_t vmo = reqs[i].vmo;
        ZX_DEBUG_ASSERT(vmo != mapper_.vmo().get());

        // Write data from the vmo into the buffer.
        void* ptr = GetData(buf_offset);

        zx_status_t status;
        ZX_DEBUG_ASSERT((start_ <= buf_offset) ?
                        (start_ < buf_offset + buf_len) :
                        (buf_offset + buf_len <= start_)); // Wraparound
        status = zx_vmo_read(vmo, ptr, vmo_offset * kMinfsBlockSize, buf_len * kMinfsBlockSize);
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "VMO Read Fail: %d", status);

        // Update the buffer length to include newly written data.
        length_ += buf_len;

        // Update the write_request to transfer from the writeback buffer out to disk,
        // rather than the supplied VMO.
        // Set the vmo handle to invalid, since we will be using the same vmoid for all requests.
        reqs[i].vmo = ZX_HANDLE_INVALID;
        reqs[i].vmo_offset = buf_offset;
        reqs[i].length = buf_len;

        if (buf_len != vmo_len) {
            // We wrapped around; write what remains from this request.
            vmo_offset += buf_len;
            dev_offset += buf_len;
            buf_len = vmo_len - buf_len;
            ZX_DEBUG_ASSERT(buf_len > 0);

            ptr = GetData(0);
            ZX_DEBUG_ASSERT((start_ == 0) ? (start_ < buf_len) : (buf_len <= start_)); // Wraparound
            status = zx_vmo_read(vmo, ptr, vmo_offset * kMinfsBlockSize, buf_len * kMinfsBlockSize);
            ZX_DEBUG_ASSERT(status == ZX_OK);

            length_ += buf_len;
            total_len += buf_len;

            // Shift down all following write requests.
            static_assert(std::is_pod<WriteRequest>::value, "Can't memmove non-POD");

            // Insert the "new" request, which is the latter half of the last request.
            WriteRequest request;
            request.vmo = vmo;
            request.vmo_offset = 0;
            request.dev_offset = dev_offset;
            request.length = buf_len;
            i++;
            reqs.insert(i, request);
        }

        // Verify that the length of all vmo writes we did match the total length we were meant to
        // write from the initial vmo.
        ZX_DEBUG_ASSERT(init_len == total_len);
    }

    write_transaction->SetBuffer(vmoid_, first_block);
}

bool Buffer::VerifyTransaction(WriteTxn* write_transaction) const {
    if (write_transaction->CheckBuffer(vmoid_)) {
        if (write_transaction->BlockCount() > 0) {
            // If the work belongs to the WritebackQueue, verify that it matches up with the
            // buffer's start/len.
            ZX_ASSERT(write_transaction->BlockStart() == start_);
            ZX_ASSERT(write_transaction->BlockCount() <= length_);
        }

        return true;
    }

    return false;
}

void Buffer::FreeSpace(blk_t blocks) {
    ZX_DEBUG_ASSERT(blocks <= length_);
    start_ = (start_ + blocks) % capacity_;
    length_ -= blocks;
}

void* Buffer::GetData(blk_t index) {
    ZX_DEBUG_ASSERT(index < capacity_);
    return (void*)((uintptr_t)mapper_.start() + (uintptr_t)(index * kMinfsBlockSize));
}

WritebackQueue::~WritebackQueue() {
    WritebackState state;

    {
        // Signal the background thread.
        fbl::AutoLock lock(&lock_);
        state = state_;
        unmounting_ = true;
        cnd_signal(&work_added_);
    }

    if (state != WritebackState::kInit) {
        // Block until the background thread completes itself.
        int r;
        thrd_join(worker_, &r);
    }

    // Ensure that all work has been completed.
    ZX_DEBUG_ASSERT(work_queue_.is_empty());
    ZX_DEBUG_ASSERT(producer_queue_.is_empty());
}

zx_status_t WritebackQueue::Create(Bcache* bc, const blk_t buffer_blocks,
                                   fbl::unique_ptr<WritebackQueue>* out) {
    zx_status_t status;
    std::unique_ptr<Buffer> buffer;
    if ((status = Buffer::Create(bc, buffer_blocks, "minfs-writeback", &buffer)) != ZX_OK) {
        return status;
    }

    fbl::unique_ptr<WritebackQueue> queue(new WritebackQueue(std::move(buffer)));

    if (thrd_create_with_name(&queue->worker_,
                              WritebackQueue::WritebackThread, queue.get(),
                              "minfs-writeback") != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    }

    fbl::AutoLock lock(&queue->lock_);
    queue->state_ = WritebackState::kRunning;
    *out = std::move(queue);
    return ZX_OK;
}

zx_status_t WritebackQueue::Enqueue(fbl::unique_ptr<WritebackWork> work) {
    TRACE_DURATION("minfs", "WritebackQueue::Enqueue");
    TRACE_FLOW_BEGIN("minfs", "writeback", reinterpret_cast<trace_flow_id_t>(work.get()));
    fbl::AutoLock lock(&lock_);
    zx_status_t status = ZX_OK;

    if (IsReadOnlyLocked()) {
        // If we are in a readonly state, return an error. However, the work should still be
        // enqueued and ultimately processed by the WritebackThread. This will help us avoid
        // potential race conditions if the work callback must acquire a lock.
        status = ZX_ERR_BAD_STATE;
    } else if (!work->IsBuffered()) {
        {
            TRACE_DURATION("minfs", "Allocating Writeback space");
            // TODO(smklein): Experimentally, all filesystem operations cause between
            // 0 and 10 blocks to be updated, though the writeback buffer has space
            // for thousands of blocks.
            //
            // Hypothetically, an operation (most likely, an enormous write) could
            // cause a single operation to exceed the size of the writeback buffer,
            // but this is currently impossible as our writes are broken into 8KB
            // chunks.
            //
            // Regardless, there should either (1) exist a fallback mechanism for these
            // extremely large operations, or (2) the worst-case operation should be
            // calculated, and it should be proven that it will always fit within
            // the allocated writeback buffer.
            EnsureSpaceLocked(work->BlockCount());
        }

        // It is possible that the queue entered a read only state
        // while we were waiting to ensure space, so check again now.
        if (IsReadOnlyLocked()) {
            status = ZX_ERR_BAD_STATE;
        } else {
            TRACE_DURATION("minfs", "Copying to Writeback buffer");
            buffer_->CopyTransaction(work.get());
        }
    }

    work_queue_.push(std::move(work));
    cnd_signal(&work_added_);
    return status;
}

void WritebackQueue::EnsureSpaceLocked(blk_t blocks) {
    while (!buffer_->IsSpaceAvailable(blocks)) {
        // Not enough room to write back work, yet. Wait until room is available.
        Waiter waiter;
        producer_queue_.push(&waiter);

        do {
            cnd_wait(&work_completed_, lock_.GetInternal());
        } while ((&producer_queue_.front() != &waiter) || // We are first in line to enqueue...
                (!buffer_->IsSpaceAvailable(blocks))); // ... and there is enough space for us.

        producer_queue_.pop();
    }
}

// Thread which asynchronously processes transactions.
int WritebackQueue::WritebackThread(void* arg) {
    WritebackQueue* writeback = reinterpret_cast<WritebackQueue*>(arg);
    writeback->ProcessLoop();
    return 0;
}

void WritebackQueue::ProcessLoop() {
    lock_.Acquire();
    while (true) {
        bool error = IsReadOnlyLocked();
        while (!work_queue_.is_empty()) {
            fbl::unique_ptr<WritebackWork> work = work_queue_.pop();
            TRACE_DURATION("minfs", "WritebackQueue::WritebackThread");

            bool our_buffer = buffer_->VerifyTransaction(work.get());

            // Stay unlocked while processing a unit of work.
            lock_.Release();

            blk_t block_count = work->BlockCount();

            if (error) {
                // If we are in a read only state, reset the work without completing it.
                work->MarkCompleted(ZX_ERR_BAD_STATE);
            } else {
                // If we should complete the work, make sure it has been buffered.
                // (This is not necessary if we are currently in an error state).
                ZX_ASSERT(work->IsBuffered());
                zx_status_t status;
                if ((status = work->Complete()) != ZX_OK) {
                    fprintf(stderr, "Work failed with status %d - "
                                    "converting writeback to read only state.\n", status);
                    // If work completion failed, set the buffer to an error state.
                    error = true;
                }
            }

            TRACE_FLOW_END("minfs", "writeback", reinterpret_cast<trace_flow_id_t>(work.get()));
            work = nullptr;
            lock_.Acquire();

            if (error) {
                // If we encountered an error, set the queue to readonly.
                state_ = WritebackState::kReadOnly;
            }

            if (our_buffer) {
                // Update the buffer's start/len accordingly.
                buffer_->FreeSpace(block_count);
            }

            // We may have opened up space (or entered a read only state),
            // so signal the producer queue.
            cnd_signal(&work_completed_);
        }

        // Before waiting, we should check if we're unmounting.
        // If work still remains in the work or producer queues,
        // continue the loop until they are empty.
        if (unmounting_ && work_queue_.is_empty() && producer_queue_.is_empty()) {
            break;
        }

        cnd_wait(&work_added_, lock_.GetInternal());
    }

    lock_.Release();
}

} // namespace minfs
