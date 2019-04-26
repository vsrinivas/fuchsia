// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/metrics.h>
#include <blobfs/write-txn.h>

namespace blobfs {

WriteTxn::~WriteTxn() {
    ZX_DEBUG_ASSERT_MSG(requests_.is_empty(), "WriteTxn still has pending requests");
}

void WriteTxn::Enqueue(const zx::vmo& vmo, uint64_t relative_block, uint64_t absolute_block,
                       uint64_t nblocks) {
    ZX_DEBUG_ASSERT(vmo.is_valid());
    ZX_DEBUG_ASSERT(!IsBuffered());

    for (auto& request : requests_) {
        if (request.vmo != vmo.get()) {
            continue;
        }

        if (request.vmo_offset == relative_block) {
            // Take the longer of the operations (if operating on the same blocks).
            if (nblocks > request.length) {
                block_count_ += (nblocks - request.length);
                request.length = nblocks;
            }
            return;
        } else if ((request.vmo_offset + request.length == relative_block) &&
                   (request.dev_offset + request.length == absolute_block)) {
            // Combine with the previous request, if immediately following.
            request.length += nblocks;
            block_count_ += nblocks;
            return;
        }
    }

    WriteRequest request;
    request.vmo = vmo.get();
    request.vmo_offset = relative_block;
    request.dev_offset = absolute_block;
    request.length = nblocks;
    requests_.push_back(std::move(request));
    block_count_ += request.length;
}

size_t WriteTxn::BlkStart() const {
    ZX_DEBUG_ASSERT(IsBuffered());
    ZX_DEBUG_ASSERT(requests_.size() > 0);
    return requests_[0].vmo_offset;
}

size_t WriteTxn::BlkCount() const {
    return block_count_;
}

void WriteTxn::SetBuffer(vmoid_t vmoid) {
    ZX_DEBUG_ASSERT(vmoid_ == VMOID_INVALID || vmoid_ == vmoid);
    ZX_DEBUG_ASSERT(vmoid != VMOID_INVALID);
    vmoid_ = vmoid;
}

zx_status_t WriteTxn::Flush() {
    ZX_ASSERT(IsBuffered());
    fs::Ticker ticker(transaction_manager_->LocalMetrics().Collecting());

    // Update all the outgoing transactions to be in disk blocks
    block_fifo_request_t blk_reqs[requests_.size()];
    const uint32_t kDiskBlocksPerBlobfsBlock =
            kBlobfsBlockSize / transaction_manager_->DeviceBlockSize();
    for (size_t i = 0; i < requests_.size(); i++) {
        blk_reqs[i].group = transaction_manager_->BlockGroupID();
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
    zx_status_t status = transaction_manager_->Transaction(blk_reqs, requests_.size());

    if (transaction_manager_->LocalMetrics().Collecting()) {
        uint64_t sum = 0;
        for (const auto& blk_req : blk_reqs) {
            sum += blk_req.length * kBlobfsBlockSize;
        }
        transaction_manager_->LocalMetrics().UpdateWriteback(sum, ticker.End());
    }

    requests_.reset();
    vmoid_ = VMOID_INVALID;
    block_count_ = 0;
    return status;
}

} // namespace blobfs
