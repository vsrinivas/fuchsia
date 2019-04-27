// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/writeback.h>

#include <utility>

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

} // namespace blobfs
