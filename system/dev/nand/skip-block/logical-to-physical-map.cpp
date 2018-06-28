// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical-to-physical-map.h"

namespace nand {

LogicalToPhysicalMap::LogicalToPhysicalMap(uint32_t block_count, fbl::Array<uint32_t> bad_blocks)
    : block_count_(block_count), bad_blocks_(fbl::move(bad_blocks)) {
    ZX_ASSERT(block_count_ >= bad_blocks_.size());
}

zx_status_t LogicalToPhysicalMap::GetPhysical(uint32_t block, uint32_t* physical_block) const {
    uint32_t i = 0;
    for (; i < bad_blocks_.size(); i++) {
        if (block + i < bad_blocks_[i]){
            *physical_block =  block + i;
            return ZX_OK;
        }
    }
    if (block + i < block_count_) {
        *physical_block =  block + i;
        return ZX_OK;
    }

    return ZX_ERR_OUT_OF_RANGE;
}

} // namespace nand
