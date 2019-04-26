// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/buffer.h>
#include <blobfs/writeback-work.h>

#include <utility>

namespace blobfs {

Buffer::~Buffer() {
    if (vmoid_ != VMOID_INVALID) {
        // Close the buffer vmo.
        block_fifo_request_t request;
        request.group = transaction_manager_->BlockGroupID();
        request.vmoid = vmoid_;
        request.opcode = BLOCKIO_CLOSE_VMO;
        transaction_manager_->Transaction(&request, 1);
    }
}

zx_status_t Buffer::Create(TransactionManager* blobfs, size_t blocks, const char* label,
                           fbl::unique_ptr<Buffer>* out) {

    fzl::OwnedVmoMapper mapper;
    zx_status_t status = mapper.CreateAndMap(blocks * kBlobfsBlockSize, "blob-writeback");
    if (status != ZX_OK) {
        FS_TRACE_ERROR("Buffer: Failed to create vmo\n");
        return status;
    }

    fbl::unique_ptr<Buffer> buffer(new Buffer(blobfs, std::move(mapper)));
    if ((status = buffer->transaction_manager_->AttachVmo(buffer->mapper_.vmo(), &buffer->vmoid_))
        != ZX_OK) {
        FS_TRACE_ERROR("Buffer: Failed to attach vmo\n");
        return status;
    }

    *out = std::move(buffer);
    return ZX_OK;
}

bool Buffer::IsSpaceAvailable(size_t blocks) const {
    // TODO(planders): Similar to minfs, make sure that we either have a fallback mechanism for
    // operations which are too large to be fully contained by the buffer, or that the
    // worst-case operation will always fit within the buffer.
    ZX_ASSERT_MSG(blocks <= capacity_, "Requested txn (%zu blocks) larger than buffer", blocks);
    return length_ + blocks <= capacity_;
}

void Buffer::CopyTransaction(WriteTxn* txn) {
    ZX_DEBUG_ASSERT(!txn->IsBuffered());
    auto& reqs = txn->Requests();

    for (size_t i = 0; i < reqs.size(); i++) {
        ZX_DEBUG_ASSERT(reqs[i].vmo != ZX_HANDLE_INVALID);

        // Read parameters of the current request.
        size_t vmo_offset = reqs[i].vmo_offset;
        size_t dev_offset = reqs[i].dev_offset;
        const size_t vmo_len = reqs[i].length;
        ZX_DEBUG_ASSERT(vmo_len > 0);

        // Calculate the offset/length we will need to write into the buffer.
        size_t buf_offset = (start_ + length_) % capacity_;
        size_t buf_len = (buf_offset + vmo_len > capacity_) ? capacity_ - buf_offset : vmo_len;
        size_t init_len = vmo_len;
        size_t total_len = buf_len;

        // Verify that the length is valid.
        ZX_DEBUG_ASSERT(buf_len > 0);
        ZX_DEBUG_ASSERT(buf_len <= vmo_len);
        ZX_DEBUG_ASSERT(buf_len < capacity_);
        zx_handle_t vmo = reqs[i].vmo;
        ZX_DEBUG_ASSERT(vmo != mapper_.vmo().get());

        // Write data from the vmo into the buffer.
        void* ptr = MutableData(buf_offset);

        zx_status_t status;
        ZX_DEBUG_ASSERT((start_ <= buf_offset) ?
                        (start_ < buf_offset + buf_len) :
                        (buf_offset + buf_len <= start_)); // Wraparound
        ZX_ASSERT_MSG((status = zx_vmo_read(vmo, ptr, vmo_offset * kBlobfsBlockSize,
                        buf_len * kBlobfsBlockSize)) == ZX_OK, "VMO Read Fail: %d", status);

        // Update the buffer len to include newly written data.
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

            ptr = MutableData(0);
            ZX_DEBUG_ASSERT((start_ == 0) ? (start_ < buf_len) : (buf_len <= start_)); // Wraparound
            ZX_ASSERT(zx_vmo_read(vmo, ptr, vmo_offset * kBlobfsBlockSize,
                                  buf_len * kBlobfsBlockSize) == ZX_OK);

            length_ += buf_len;
            total_len += buf_len;

            // Shift down all following write requests.
            static_assert(std::is_pod<WriteRequest>::value, "Can't memmove non-POD");

            // Shift down all following write requests
            static_assert(std::is_pod<WriteRequest>::value, "Can't memmove non-POD");
            // Insert the "new" request, which is the latter half of the last request
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

    txn->SetBuffer(vmoid_);
}

void Buffer::AddTransaction(size_t start, size_t disk_start, size_t length, WritebackWork* work) {
    // Ensure the request fits within the buffer.
    ZX_DEBUG_ASSERT(length > 0);
    ZX_DEBUG_ASSERT(start + length <= capacity_);
    ZX_DEBUG_ASSERT(work != nullptr);
    work->Enqueue(mapper_.vmo(), start, disk_start, length);
}

bool Buffer::VerifyTransaction(WriteTxn* txn) const {
    if (txn->CheckBuffer(vmoid_)) {
        if (txn->BlkCount() > 0) {
            // If the work belongs to the WritebackQueue, verify that it matches up with the
            // buffer's start/len.
            ZX_ASSERT(txn->BlkStart() == start_);
            ZX_ASSERT(txn->BlkCount() <= length_);
        }

        return true;
    }

    return false;
}

void Buffer::ValidateTransaction(WriteTxn* txn) {
    if (txn->IsBuffered()) {
        // If transaction is already buffered, make sure it belongs to this buffer.
        ZX_DEBUG_ASSERT(txn->CheckBuffer(vmoid_));
    } else {
        fbl::Vector<WriteRequest>& reqs = txn->Requests();

        for (size_t i = 0; i < reqs.size(); i++) {
            // Verify that each request references this buffer VMO,
            // and that the transaction fits within the buffer.
            ZX_DEBUG_ASSERT(reqs[i].vmo == mapper_.vmo().get());
            reqs[i].vmo = ZX_HANDLE_INVALID;
        }

        // Once each request has been verified, set the buffer.
        txn->SetBuffer(vmoid_);
    }
}

void Buffer::FreeSpace(size_t blocks) {
    ZX_DEBUG_ASSERT(blocks <= length_);
    start_ = (start_ + blocks) % capacity_;
    length_ -= blocks;
}

} // namespace blobfs
