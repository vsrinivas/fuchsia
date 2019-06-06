// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/writeback.h>

#include <utility>
#include <vector>

namespace blobfs {

zx_status_t EnqueuePaginated(fbl::unique_ptr<WritebackWork>* work,
                             TransactionManager* transaction_manager, Blob* vn,
                             const zx::vmo& vmo, uint64_t relative_block, uint64_t absolute_block,
                             uint64_t nblocks) {
    const size_t kMaxChunkBlocks = (3 * transaction_manager->WritebackCapacity()) / 4;
    uint64_t delta_blocks = fbl::min(nblocks, kMaxChunkBlocks);
    while (nblocks > 0) {
        if ((*work)->Transaction().BlkCount() + delta_blocks > kMaxChunkBlocks) {
            // If enqueueing these blocks could push us past the writeback buffer capacity
            // when combined with all previous writes, break this transaction into a smaller
            // chunk first.
            fbl::unique_ptr<WritebackWork> tmp;
            zx_status_t status = transaction_manager->CreateWork(&tmp, vn);
            if (status != ZX_OK) {
                return status;
            }
            if ((status = transaction_manager->EnqueueWork(std::move(*work),
                                                           EnqueueType::kData)) != ZX_OK) {
                return status;
            }
            *work = std::move(tmp);
        }

        (*work)->Transaction().Enqueue(vmo, relative_block, absolute_block, delta_blocks);
        relative_block += delta_blocks;
        absolute_block += delta_blocks;
        nblocks -= delta_blocks;
        delta_blocks = fbl::min(nblocks, kMaxChunkBlocks);
    }
    return ZX_OK;
}

zx_status_t FlushWriteRequests(TransactionManager* transaction_manager,
                               const fbl::Vector<BufferedOperation>& operations) {
    if (operations.is_empty()) {
        return ZX_OK;
    }

    fs::Ticker ticker(transaction_manager->LocalMetrics().Collecting());

    // Update all the outgoing transactions to be in disk blocks.
    std::vector<block_fifo_request_t> blk_reqs;
    blk_reqs.resize(operations.size());
    const uint32_t kDiskBlocksPerBlobfsBlock =
        transaction_manager->FsBlockSize() / transaction_manager->DeviceBlockSize();
    for (size_t i = 0; i < operations.size(); i++) {
        blk_reqs[i].group = transaction_manager->BlockGroupID();
        blk_reqs[i].vmoid = operations[i].vmoid;
        blk_reqs[i].opcode = BLOCKIO_WRITE;
        blk_reqs[i].vmo_offset = operations[i].op.vmo_offset * kDiskBlocksPerBlobfsBlock;
        blk_reqs[i].dev_offset = operations[i].op.dev_offset * kDiskBlocksPerBlobfsBlock;
        uint64_t length = operations[i].op.length * kDiskBlocksPerBlobfsBlock;
        // TODO(ZX-2253): Requests this long, although unlikely, should be
        // handled more gracefully.
        ZX_ASSERT_MSG(length < UINT32_MAX, "Request size too large");
        blk_reqs[i].length = static_cast<uint32_t>(length);
    }

    // Actually send the operations to the underlying block device.
    zx_status_t status = transaction_manager->Transaction(&blk_reqs[0], operations.size());

    if (transaction_manager->LocalMetrics().Collecting()) {
        uint64_t sum = 0;
        for (const auto& blk_req : blk_reqs) {
            sum += blk_req.length * transaction_manager->FsBlockSize();
        }
        transaction_manager->LocalMetrics().UpdateWriteback(sum, ticker.End());
    }

    return status;
}

} // namespace blobfs
