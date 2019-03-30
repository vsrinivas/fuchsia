// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vnode-allocation.h"

namespace minfs {

zx_status_t PendingAllocationData::GetNextRange(blk_t* start, blk_t* count) const {
    auto range = block_map_.begin();

    if (range == block_map_.end()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    *start = static_cast<blk_t>(range->bitoff);
    *count = static_cast<blk_t>(range->bitlen);
    return ZX_OK;
}

blk_t PendingAllocationData::GetLongestRange() const {
    blk_t block_count = 0;
    for (auto range = block_map_.begin(); range != block_map_.end(); range++) {
        if (range->bitlen > block_count) {
            block_count = static_cast<blk_t>(range->bitlen);
        }
    }
    return block_count;
}

bool PendingAllocationData::SetPending(blk_t block_num) {
    size_t initial_bits = block_map_.num_bits();
    ZX_ASSERT(block_map_.SetOne(block_num) == ZX_OK);
    return block_map_.num_bits() > initial_bits;
}

bool PendingAllocationData::ClearPending(blk_t block_num) {
    size_t initial_bits = block_map_.num_bits();
    ZX_ASSERT(block_map_.ClearOne(block_num) == ZX_OK);
    return block_map_.num_bits() < initial_bits;
}

} // namespace minfs
