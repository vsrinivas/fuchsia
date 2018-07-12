// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/blobfs.h>
#include <blobfs/writeback.h>

namespace blobfs {

void WriteTxn::Enqueue(zx_handle_t vmo, uint64_t relative_block, uint64_t absolute_block,
                       uint64_t nblocks) {
    ZX_DEBUG_ASSERT(!IsReady());

    for (size_t i = 0; i < requests_.size(); i++) {
        if (requests_[i].vmo != vmo) {
            continue;
        }

        if (requests_[i].vmo_offset == relative_block) {
            // Take the longer of the operations (if operating on the same blocks).
            requests_[i].length = (requests_[i].length > nblocks) ? requests_[i].length : nblocks;
            return;
        } else if ((requests_[i].vmo_offset + requests_[i].length == relative_block) &&
                   (requests_[i].dev_offset + requests_[i].length == absolute_block)) {
            // Combine with the previous request, if immediately following.
            requests_[i].length += nblocks;
            return;
        }
    }

    write_request_t request;
    request.vmo = vmo;
    request.vmo_offset = relative_block;
    request.dev_offset = absolute_block;
    request.length = nblocks;
    requests_.push_back(fbl::move(request));
}

zx_status_t WriteTxn::Flush() {
    ZX_ASSERT(IsReady());
    fs::Ticker ticker(bs_->CollectingMetrics());

    // Update all the outgoing transactions to be in disk blocks
    block_fifo_request_t blk_reqs[requests_.size()];
    const uint32_t kDiskBlocksPerBlobfsBlock = kBlobfsBlockSize / bs_->BlockSize();
    for (size_t i = 0; i < requests_.size(); i++) {
        blk_reqs[i].group = bs_->BlockGroupID();
        blk_reqs[i].vmoid = vmoid_;
        blk_reqs[i].opcode = BLOCKIO_WRITE;
        blk_reqs[i].vmo_offset = requests_[i].vmo_offset * kDiskBlocksPerBlobfsBlock;
        blk_reqs[i].dev_offset = requests_[i].dev_offset * kDiskBlocksPerBlobfsBlock;
        uint64_t length = requests_[i].length * kDiskBlocksPerBlobfsBlock;
        // TODO(ZX-2253): Requests this long, although unlikely, should be
        // handled more gracefully.
        ZX_ASSERT_MSG(length < UINT32_MAX, "Request size too large");
        blk_reqs[i].length = static_cast<uint32_t>(length);
    }

    // Actually send the operations to the underlying block device.
    zx_status_t status = bs_->Txn(blk_reqs, requests_.size());

    if (bs_->CollectingMetrics()) {
        uint64_t sum = 0;
        for (size_t i = 0; i < requests_.size(); i++) {
            sum += blk_reqs[i].length * kBlobfsBlockSize;
        }
        bs_->UpdateWritebackMetrics(sum, ticker.End());
    }

    requests_.reset();
    vmoid_ = VMOID_INVALID;
    return status;
}

size_t WriteTxn::BlkStart() const {
    ZX_DEBUG_ASSERT(IsReady());
    ZX_DEBUG_ASSERT(requests_.size() > 0);
    return requests_[0].vmo_offset;
}

size_t WriteTxn::BlkCount() const {
    size_t blocks_needed = 0;
    for (size_t i = 0; i < requests_.size(); i++) {
        blocks_needed += requests_[i].length;
    }
    return blocks_needed;
}

WritebackWork::WritebackWork(Blobfs* bs, fbl::RefPtr<VnodeBlob> vn) :
    WriteTxn(bs), closure_(nullptr), sync_(false), vn_(fbl::move(vn)) {}

void WritebackWork::Reset() {
    ZX_DEBUG_ASSERT(Requests().is_empty());
    closure_ = nullptr;
    vn_ = nullptr;
}

void WritebackWork::SetSyncComplete() {
    ZX_ASSERT(vn_);
    sync_ = true;
}

// Returns the number of blocks of the writeback buffer that have been consumed
zx_status_t WritebackWork::Complete() {
    zx_status_t status = Flush();

    //TODO(planders): On flush failure, convert fs to read-only
    if (status == ZX_OK && sync_) {
        vn_->CompleteSync();
    }

    if (closure_) {
        closure_(status);
    }

    Reset();
    return ZX_OK;
}

void WritebackWork::SetClosure(SyncCallback closure) {
    ZX_DEBUG_ASSERT(!closure_);
    closure_ = fbl::move(closure);
}

zx_status_t WritebackBuffer::Create(Blobfs* bs, fbl::unique_ptr<fzl::MappedVmo> buffer,
                                    fbl::unique_ptr<WritebackBuffer>* out) {
    fbl::unique_ptr<WritebackBuffer> wb(new WritebackBuffer(bs, fbl::move(buffer)));
    if (wb->buffer_->GetSize() % kBlobfsBlockSize != 0) {
        return ZX_ERR_INVALID_ARGS;
    } else if (cnd_init(&wb->consumer_cvar_) != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    } else if (cnd_init(&wb->producer_cvar_) != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    } else if (thrd_create_with_name(&wb->writeback_thrd_,
                                     WritebackBuffer::WritebackThread, wb.get(),
                                     "blobfs-writeback") != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    }
    zx_status_t status = wb->bs_->AttachVmo(wb->buffer_->GetVmo(), &wb->buffer_vmoid_);
    if (status != ZX_OK) {
        return status;
    }

    *out = fbl::move(wb);
    return ZX_OK;
}

zx_status_t WritebackBuffer::GenerateWork(fbl::unique_ptr<WritebackWork>* out,
                                          fbl::RefPtr<VnodeBlob> vnode) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<WritebackWork> wb(new (&ac) WritebackWork(bs_, fbl::move(vnode)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *out = fbl::move(wb);
    return ZX_OK;
}

WritebackBuffer::WritebackBuffer(Blobfs* bs, fbl::unique_ptr<fzl::MappedVmo> buffer) :
    bs_(bs), unmounting_(false), buffer_(fbl::move(buffer)),
    cap_(buffer_->GetSize() / kBlobfsBlockSize) {}

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
        request.group = bs_->BlockGroupID();
        request.vmoid = buffer_vmoid_;
        request.opcode = BLOCKIO_CLOSE_VMO;
        bs_->Txn(&request, 1);
    }
}

zx_status_t WritebackBuffer::EnsureSpaceLocked(size_t blocks) {
    if (blocks > cap_) {
        // There will never be enough room in the writeback buffer for this request.
        return ZX_ERR_NO_RESOURCES;
    }
    while (len_ + blocks > cap_) {
        // Not enough room to write back work, yet. Wait until room is available.
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
    ZX_DEBUG_ASSERT(!txn->IsReady());

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
                            (uintptr_t)(wb_offset * kBlobfsBlockSize));
        zx_status_t status;
        ZX_DEBUG_ASSERT((start_ <= wb_offset) ?
                        (start_ < wb_offset + wb_len) :
                        (wb_offset + wb_len <= start_)); // Wraparound
        ZX_ASSERT_MSG((status = zx_vmo_read(vmo, ptr, vmo_offset * kBlobfsBlockSize,
                      wb_len * kBlobfsBlockSize)) == ZX_OK, "VMO Read Fail: %d", status);

        len_ += wb_len;

        // Update the write_request to transfer from the writeback buffer out to disk, rather than
        // the supplied VMO

        reqs[i].vmo_offset = wb_offset;
        reqs[i].length = wb_len;

        if (wb_len != vmo_len) {
            // We wrapped around; write what remains from this request
            vmo_offset += wb_len;
            dev_offset += wb_len;
            wb_len = vmo_len - wb_len;
            ptr = buffer_->GetData();
            ZX_DEBUG_ASSERT((start_ == 0) ? (start_ < wb_len) : (wb_len <= start_)); // Wraparound
            ZX_ASSERT(zx_vmo_read(vmo, ptr, vmo_offset * kBlobfsBlockSize,
                                  wb_len * kBlobfsBlockSize) == ZX_OK);

            len_ += wb_len;

            // Shift down all following write requests
            static_assert(fbl::is_pod<write_request_t>::value, "Can't memmove non-POD");
            // Insert the "new" request, which is the latter half of the last request
            write_request_t request;
            request.vmo = reqs[i].vmo;
            request.vmo_offset = 0;
            request.dev_offset = dev_offset;
            request.length = wb_len;
            i++;
            reqs.insert(i, request);
        }
    }

    txn->SetReady(buffer_vmoid_);
}

void WritebackBuffer::Enqueue(fbl::unique_ptr<WritebackWork> work) {
    TRACE_DURATION("blobfs", "WritebackBuffer::Enqueue", "work ptr", work.get());
    fbl::AutoLock lock(&writeback_lock_);
    size_t blocks = work->BlkCount();
    // TODO(planders): Similar to minfs, make sure that we either have a fallback mechanism for
    // operations which are too large to be fully contained by the buffer, or that the
    // worst-case operation will always fit within the buffer
    ZX_ASSERT_MSG(EnsureSpaceLocked(blocks) == ZX_OK,
                "Requested txn (%zu blocks) larger than writeback buffer", blocks);
    CopyToBufferLocked(work.get());
    work_queue_.push(fbl::move(work));
    cnd_signal(&consumer_cvar_);
}

int WritebackBuffer::WritebackThread(void* arg) {
    WritebackBuffer* b = reinterpret_cast<WritebackBuffer*>(arg);

    b->writeback_lock_.Acquire();
    while (true) {
        while (!b->work_queue_.is_empty()) {
            auto work = b->work_queue_.pop();
            TRACE_DURATION("blobfs", "WritebackBuffer::WritebackThread", "work ptr", work.get());

            size_t blk_count = work->BlkCount();

            if (blk_count > 0) {
                ZX_ASSERT(work->BlkStart() == b->start_);
                ZX_ASSERT(blk_count <= b->len_);
            }

            // Stay unlocked while processing a unit of work
            b->writeback_lock_.Release();
            work->Complete();
            work = nullptr;
            b->writeback_lock_.Acquire();
            b->start_ = (b->start_ + blk_count) % b->cap_;
            b->len_ -= blk_count;
            cnd_signal(&b->producer_cvar_);
        }

        // Before waiting, we should check if we're unmounting.
        if (b->unmounting_) {
            b->writeback_lock_.Release();
            return 0;
        }
        cnd_wait(&b->consumer_cvar_, b->writeback_lock_.GetInternal());
    }
}
} // namespace blobfs
