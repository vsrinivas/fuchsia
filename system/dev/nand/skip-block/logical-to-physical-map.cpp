// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logical-to-physical-map.h"

#include <fbl/algorithm.h>

namespace nand {

LogicalToPhysicalMap::LogicalToPhysicalMap(uint32_t copies, uint32_t block_count,
                                           fbl::Array<uint32_t> bad_blocks)
    : copies_(copies), block_count_(block_count), bad_blocks_(fbl::move(bad_blocks)) {
    ZX_ASSERT(block_count_ > 0);
    ZX_ASSERT(block_count_ >= bad_blocks_.size());
    ZX_ASSERT(block_count_ % copies_ == 0);

    qsort(bad_blocks_.get(), bad_blocks_.size(), sizeof(uint32_t),
          [](const void* l, const void* r) {
              const auto* left = static_cast<const uint32_t*>(l);
              const auto* right = static_cast<const uint32_t*>(r);
              if (*left < *right) {
                  return -1;
              } else if (*left > *right) {
                  return 1;
              }
              return 0;
          });
}

zx_status_t LogicalToPhysicalMap::GetPhysical(uint32_t copy, uint32_t block,
                                              uint32_t* physical_block) const {
    ZX_ASSERT(copy < copies_);

    const uint32_t blocks_per_copy = block_count_ / copies_;
    const uint32_t first = copy * blocks_per_copy;
    const uint32_t last = first + blocks_per_copy - 1;
    block += first;
    uint32_t skipped_blocks = 0;
    for (const auto& bad_block : bad_blocks_) {
        if (bad_block != fbl::clamp(bad_block, first, last)) {
            continue;
        }

        if (block + skipped_blocks < bad_block) {
            *physical_block = block + skipped_blocks;
            return ZX_OK;
        }
        skipped_blocks++;
    }
    if (block + skipped_blocks <= last) {
        *physical_block = block + skipped_blocks;
        return ZX_OK;
    }

    return ZX_ERR_OUT_OF_RANGE;
}

uint32_t LogicalToPhysicalMap::LogicalBlockCount(uint32_t copy) const {
    ZX_ASSERT(copy < copies_);
    const uint32_t blocks_per_copy = block_count_ / copies_;
    const uint32_t first = copy * blocks_per_copy;
    const uint32_t last = first + blocks_per_copy - 1;

    uint32_t bad_block_count = 0;
    for (const auto& bad_block : bad_blocks_) {
        if (bad_block == fbl::clamp(bad_block, first, last)) {
            bad_block_count++;
        }
    }
    return blocks_per_copy - bad_block_count;
}

} // namespace nand
