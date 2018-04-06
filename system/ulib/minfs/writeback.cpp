// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#ifdef __Fuchsia__
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <lib/zx/vmo.h>
#endif

#include <fbl/algorithm.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>
#include <fs/mapped-vmo.h>
#include <fs/vfs.h>

#include "minfs-private.h"
#include <minfs/writeback.h>

namespace minfs {

#ifdef __Fuchsia__

void WriteTxn::Enqueue(zx_handle_t vmo, uint64_t vmo_offset, uint64_t dev_offset,
                       uint64_t nblocks) {
    validate_vmo_size(vmo, static_cast<blk_t>(vmo_offset));
    for (size_t i = 0; i < requests_.size(); i++) {
        if (requests_[i].vmo != vmo) {
            continue;
        }

        if (requests_[i].vmo_offset == vmo_offset) {
            // Take the longer of the operations (if operating on the same
            // blocks).
            requests_[i].length = (requests_[i].length > nblocks) ? requests_[i].length : nblocks;
            return;
        } else if ((requests_[i].vmo_offset + requests_[i].length == vmo_offset) &&
                   (requests_[i].dev_offset + requests_[i].length == dev_offset)) {
            // Combine with the previous request, if immediately following.
            requests_[i].length += nblocks;
            return;
        }
    }

    write_request_t request;
    request.vmo = vmo;
    // NOTE: It's easier to compare everything when dealing
    // with blocks (not offsets!) so the following are described in
    // terms of blocks until we Flush().
    request.vmo_offset = vmo_offset;
    request.dev_offset = dev_offset;
    request.length = nblocks;
    requests_.push_back(fbl::move(request));
}

zx_status_t WriteTxn::Flush(zx_handle_t vmo, vmoid_t vmoid) {
    ZX_DEBUG_ASSERT(vmo != ZX_HANDLE_INVALID);
    ZX_DEBUG_ASSERT(vmoid != VMOID_INVALID);

    // Update all the outgoing transactions to be in "disk blocks",
    // not "Minfs blocks".
    block_fifo_request_t blk_reqs[requests_.size()];
    const uint32_t kDiskBlocksPerMinfsBlock = kMinfsBlockSize / bc_->BlockSize();
    for (size_t i = 0; i < requests_.size(); i++) {
        blk_reqs[i].txnid = bc_->TxnId();
        blk_reqs[i].vmoid = vmoid;
        blk_reqs[i].opcode = BLOCKIO_WRITE;
        blk_reqs[i].vmo_offset = requests_[i].vmo_offset * kDiskBlocksPerMinfsBlock;
        blk_reqs[i].dev_offset = requests_[i].dev_offset * kDiskBlocksPerMinfsBlock;
        blk_reqs[i].length = requests_[i].length * kDiskBlocksPerMinfsBlock;
    }

    // Actually send the operations to the underlying block device.
    zx_status_t status = bc_->Txn(blk_reqs, requests_.size());

    requests_.reset();
    return status;
}

size_t WriteTxn::BlkCount() const {
    size_t blocks_needed = 0;
    for (size_t i = 0; i < requests_.size(); i++) {
        blocks_needed += requests_[i].length;
    }
    return blocks_needed;
}

#endif  // __Fuchsia__

WritebackWork::WritebackWork(Bcache* bc) : WriteTxn(bc),
#ifdef __Fuchsia__
    closure_(nullptr),
#endif
    node_count_(0) {}

void WritebackWork::Reset() {
#ifdef __Fuchsia__
    ZX_DEBUG_ASSERT(Requests().size() == 0);
    closure_ = nullptr;
#endif
    while (0 < node_count_) {
        vn_[--node_count_] = nullptr;
    }
}

#ifdef __Fuchsia__
// Returns the number of blocks of the writeback buffer that have been
// consumed
size_t WritebackWork::Complete(zx_handle_t vmo, vmoid_t vmoid) {
    size_t blk_count = BlkCount();
    zx_status_t status = Flush(vmo, vmoid);
    if (closure_) {
        closure_(status);
    }
    Reset();
    return blk_count;
}

void WritebackWork::SetClosure(SyncCallback closure) {
    ZX_DEBUG_ASSERT(!closure_);
    closure_ = fbl::move(closure);
}
#else
void WritebackWork::Complete() {
    Flush();
    Reset();
}
#endif  // __Fuchsia__

// Allow "pinning" Vnodes so they aren't destroyed while we're completing
// this writeback operation.
void WritebackWork::PinVnode(fbl::RefPtr<VnodeMinfs> vn) {
    for (size_t i = 0; i < node_count_; i++) {
        if (vn_[i].get() == vn.get()) {
            // Already pinned
            return;
        }
    }
    ZX_DEBUG_ASSERT(node_count_ < fbl::count_of(vn_));
    vn_[node_count_++] = fbl::move(vn);
}

#ifdef __Fuchsia__

zx_status_t WritebackBuffer::Create(Bcache* bc, fbl::unique_ptr<MappedVmo> buffer,
                                    fbl::unique_ptr<WritebackBuffer>* out) {
    fbl::unique_ptr<WritebackBuffer> wb(new WritebackBuffer(bc, fbl::move(buffer)));
    if (wb->buffer_->GetSize() % kMinfsBlockSize != 0) {
        return ZX_ERR_INVALID_ARGS;
    } else if (cnd_init(&wb->consumer_cvar_) != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    } else if (cnd_init(&wb->producer_cvar_) != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    } else if (thrd_create_with_name(&wb->writeback_thrd_,
                                     WritebackBuffer::WritebackThread, wb.get(),
                                     "minfs-writeback") != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    }
    zx_status_t status = wb->bc_->AttachVmo(wb->buffer_->GetVmo(), &wb->buffer_vmoid_);
    if (status != ZX_OK) {
        return status;
    }

    *out = fbl::move(wb);
    return ZX_OK;
}

WritebackBuffer::WritebackBuffer(Bcache* bc, fbl::unique_ptr<MappedVmo> buffer) :
    bc_(bc), unmounting_(false), buffer_(fbl::move(buffer)),
    cap_(buffer_->GetSize() / kMinfsBlockSize) {}

WritebackBuffer::~WritebackBuffer() {
    // Block until the background thread completes itself.
    {
        fbl::AutoLock lock(&writeback_lock_);
        unmounting_ = true;
        cnd_signal(&consumer_cvar_);
    }
    int r;
    thrd_join(writeback_thrd_, &r);

    if (buffer_vmoid_ != VMOID_INVALID) {
        block_fifo_request_t request;
        request.txnid = bc_->TxnId();
        request.vmoid = buffer_vmoid_;
        request.opcode = BLOCKIO_CLOSE_VMO;
        bc_->Txn(&request, 1);
    }
}

zx_status_t WritebackBuffer::EnsureSpaceLocked(size_t blocks) {
    if (blocks > cap_) {
        // There will never be enough room in the writeback buffer
        // for this request.
        return ZX_ERR_NO_RESOURCES;
    }
    while (len_ + blocks > cap_) {
        // Not enough room to write back work, yet. Wait until
        // room is available.
        Waiter w;
        producer_queue_.push(&w);

        do {
            cnd_wait(&producer_cvar_, writeback_lock_.GetInternal());
        } while ((&producer_queue_.front() != &w) && // We are first in line to enqueue...
                 (len_ + blocks > cap_)); // ... and there is enough space for us.

        producer_queue_.pop();
    }
    return ZX_OK;
}

void WritebackBuffer::CopyToBufferLocked(WriteTxn* txn) {
    auto& reqs = txn->Requests();
    // Write back to the buffer
    for (size_t i = 0; i < reqs.size(); i++) {
        size_t vmo_offset = reqs[i].vmo_offset;
        size_t dev_offset = reqs[i].dev_offset;
        const size_t vmo_len = reqs[i].length;
        ZX_DEBUG_ASSERT(vmo_len > 0);
        size_t wb_offset = (start_ + len_) % cap_;
        size_t wb_len = (wb_offset + vmo_len > cap_) ? cap_ - wb_offset : vmo_len;
        ZX_DEBUG_ASSERT(wb_len <= vmo_len);
        ZX_DEBUG_ASSERT(wb_offset < cap_);
        zx_handle_t vmo = reqs[i].vmo;

        void* ptr = (void*)((uintptr_t)(buffer_->GetData()) +
                            (uintptr_t)(wb_offset * kMinfsBlockSize));
        zx_status_t status;
        ZX_DEBUG_ASSERT((start_ <= wb_offset) ?
                        (start_ < wb_offset + wb_len) :
                        (wb_offset + wb_len <= start_)); // Wraparound
        ZX_ASSERT_MSG((status = zx_vmo_read(vmo, ptr, vmo_offset * kMinfsBlockSize,
                      wb_len * kMinfsBlockSize)) == ZX_OK, "VMO Read Fail: %d", status);
        len_ += wb_len;

        // Update the write_request to transfer from the writeback buffer
        // out to disk, rather than the supplied VMO
        reqs[i].vmo_offset = wb_offset;
        reqs[i].length = wb_len;

        if (wb_len != vmo_len) {
            // We wrapped around; write what remains from this request
            vmo_offset += wb_len;
            dev_offset += wb_len;
            wb_len = vmo_len - wb_len;
            ptr = buffer_->GetData();
            ZX_DEBUG_ASSERT((start_ == 0) ?  (start_ < wb_len) : (wb_len <= start_)); // Wraparound
            ZX_ASSERT(zx_vmo_read(vmo, ptr, vmo_offset * kMinfsBlockSize,
                                  wb_len * kMinfsBlockSize) == ZX_OK);
            len_ += wb_len;

            // Shift down all following write requests
            static_assert(fbl::is_pod<write_request_t>::value, "Can't memmove non-POD");

            // Insert the "new" request, which is the latter half of
            // the request we wrote out earlier
            write_request_t request;
            request.vmo = reqs[i].vmo;
            request.vmo_offset = 0;
            request.dev_offset = dev_offset;
            request.length = wb_len;
            i++;
            reqs.insert(i, request);
        }
    }
}

void WritebackBuffer::Enqueue(fbl::unique_ptr<WritebackWork> work) {
    TRACE_DURATION("minfs", "WritebackBuffer::Enqueue");
    TRACE_FLOW_BEGIN("minfs", "writeback", reinterpret_cast<trace_flow_id_t>(work.get()));
    fbl::AutoLock lock(&writeback_lock_);

    {
        TRACE_DURATION("minfs", "Allocating Writeback space");
        size_t blocks = work->BlkCount();
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
        ZX_ASSERT_MSG(EnsureSpaceLocked(blocks) == ZX_OK,
                      "Requested txn (%zu blocks) larger than writeback buffer", blocks);
    }

    {
        TRACE_DURATION("minfs", "Copying to Writeback buffer");
        CopyToBufferLocked(work.get());
    }

    work_queue_.push(fbl::move(work));
    cnd_signal(&consumer_cvar_);
}

int WritebackBuffer::WritebackThread(void* arg) {
    WritebackBuffer* b = reinterpret_cast<WritebackBuffer*>(arg);

    b->writeback_lock_.Acquire();
    while (true) {
        while (!b->work_queue_.is_empty()) {
            auto work = b->work_queue_.pop();
            TRACE_DURATION("minfs", "WritebackBuffer::WritebackThread");

            // Stay unlocked while processing a unit of work
            b->writeback_lock_.Release();

            // TODO(smklein): We could add additional validation that the blocks
            // in "work" are contiguous and in the range of [start_, len_) (including
            // wraparound).
            size_t blks_consumed = work->Complete(b->buffer_->GetVmo(), b->buffer_vmoid_);
            TRACE_FLOW_END("minfs", "writeback", reinterpret_cast<trace_flow_id_t>(work.get()));
            work = nullptr;

            // Relock before checking the state of the queue
            b->writeback_lock_.Acquire();
            b->start_ = (b->start_ + blks_consumed) % b->cap_;
            b->len_ -= blks_consumed;
            cnd_signal(&b->producer_cvar_);
        }

        // Before waiting, we should check if we're unmounting.
        if (b->unmounting_) {
            b->writeback_lock_.Release();
            b->bc_->FreeTxnId();
            return 0;
        }
        cnd_wait(&b->consumer_cvar_, b->writeback_lock_.GetInternal());
    }
}

#endif  // __Fuchsia__

} // namespace minfs
