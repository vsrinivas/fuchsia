// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/vnode_allocation.h"

namespace minfs {

void PendingAllocationData::Reset(blk_t size) {
  new_blocks_ = 0;
  node_size_ = size;
  block_map_.ClearAll();
}

zx_status_t PendingAllocationData::GetNextRange(blk_t* start, blk_t* count) const {
  auto range = block_map_.begin();

  if (range == block_map_.end()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *start = static_cast<blk_t>(range->bitoff);
  *count = static_cast<blk_t>(range->bitlen);
  return ZX_OK;
}

void PendingAllocationData::SetPending(blk_t block_num, bool allocated) {
  size_t initial_bits = block_map_.num_bits();
  ZX_ASSERT(block_map_.SetOne(block_num) == ZX_OK);
  if (block_map_.num_bits() > initial_bits && !allocated) {
    new_blocks_++;
  }
}

bool PendingAllocationData::ClearPending(blk_t block_num, bool allocated) {
  size_t initial_bits = block_map_.num_bits();
  ZX_ASSERT(block_map_.ClearOne(block_num) == ZX_OK);

  if (block_map_.num_bits() < initial_bits) {
    if (!allocated) {
      ZX_ASSERT(new_blocks_ > 0);
      new_blocks_--;
    }
    return true;
  }

  return false;
}

}  // namespace minfs
