// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "nandpart-utils.h"

#include <stdlib.h>
#include <string.h>

#ifdef TEST
#include <unittest/unittest.h>
#define zxlogf(flags, ...) unittest_printf(__VA_ARGS__)
#else
#include <ddk/debug.h>
#endif

#include <fbl/algorithm.h>
#include <zircon/assert.h>

// Checks that the partition map is valid, sorts it in partition order, and
// ensures blocks are on erase block boundaries.
zx_status_t SanitizePartitionMap(zbi_partition_map_t* pmap, const nand_info_t& nand_info) {
    if (pmap->partition_count == 0) {
        zxlogf(ERROR, "nandpart: partition count is zero\n");
        return ZX_ERR_INTERNAL;
    }

    auto* const begin = &pmap->partitions[0];
    const auto* const end = &pmap->partitions[pmap->partition_count];

    // 1) Last block must be greater than first for each partition entry.
    for (auto* part = begin; part != end; part++) {
        if (part->first_block > part->last_block) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    // 2) Partitions should be in order.
    qsort(pmap->partitions, pmap->partition_count, sizeof(zbi_partition_t),
          [](const void* left, const void* right) {
              const auto* left_ = static_cast<const zbi_partition_t*>(left);
              const auto* right_ = static_cast<const zbi_partition_t*>(right);
              if (left_->first_block < right_->first_block) {
                  return -1;
              }
              if (left_->first_block > right_->first_block) {
                  return 1;
              }
              return 0;
          });

    // 3) Partitions should not be overlapping.
    for (auto *part = begin, *next = begin + 1; next != end; part++, next++) {
        if (part->last_block >= next->first_block) {
            zxlogf(ERROR, "nandpart: partition %s [%lu, %lu] overlaps partition %s [%lu, %lu]\n",
                   part->name, part->first_block, part->last_block, next->name, next->first_block,
                   next->last_block);
            return ZX_ERR_INTERNAL;
        }
    }

    // 4) All partitions must start at an erase block boundary.
    const size_t erase_block_size = nand_info.page_size * nand_info.pages_per_block;
    ZX_DEBUG_ASSERT(fbl::is_pow2(erase_block_size));
    const int block_shift = ffs(static_cast<int>(erase_block_size)) - 1;

    if (pmap->block_size != erase_block_size) {
        for (auto* part = begin; part != end; part++) {
            uint64_t first_byte_offset = part->first_block * pmap->block_size;
            uint64_t last_byte_offset = (part->last_block + 1) * pmap->block_size;

            if (fbl::round_down(first_byte_offset, erase_block_size) != first_byte_offset ||
                fbl::round_down(last_byte_offset, erase_block_size) != last_byte_offset) {
                zxlogf(ERROR, "nandpart: partition %s size is not a multiple of erase_block_size\n",
                       part->name);
                return ZX_ERR_INTERNAL;
            }
            part->first_block = first_byte_offset >> block_shift;
            part->last_block = (last_byte_offset >> block_shift) - 1;
        }
    }
    // 5) Partitions should exist within NAND.
    if ((end - 1)->last_block >= nand_info.num_blocks) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return ZX_OK;
}

