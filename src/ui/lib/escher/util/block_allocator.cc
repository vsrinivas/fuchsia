// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/block_allocator.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/lib/escher/util/align.h"

namespace escher {

BlockAllocator::BlockAllocator(size_t fixed_size_block_size)
    : fixed_size_block_size_(fixed_size_block_size),
      current_fixed_size_block_(
          fixed_size_blocks_.emplace(fixed_size_blocks_.end(), fixed_size_block_size_)) {}

void* BlockAllocator::Allocate(size_t size, size_t alignment) {
  // Any allocation bigger than 1/4 of the fixed-block size is treated as a
  // large block allocation.  This guarantees that no more than 1/4 of a block's
  // space is wasted, without having undesirably small "large blocks".
  if (size > fixed_size_block_size_ / 4) {
    auto it = InsertLargeBlock(size, alignment);
    void* result = AllocateFromBlock(it, size, alignment);
    FX_DCHECK(result);
    return result;
  } else if (void* result = AllocateFromBlock(current_fixed_size_block_, size, alignment)) {
    return result;
  } else {
    result = AllocateFromBlock(ObtainNextFixedSizeBlock(), size, alignment);
    FX_DCHECK(result);
    return result;
  }
}

void BlockAllocator::Reset() {
  large_blocks_.clear();
  for (auto& b : fixed_size_blocks_) {
    b.current_ptr = b.start;
  }
  current_fixed_size_block_ = fixed_size_blocks_.begin();
}

BlockAllocator::BlockList::iterator BlockAllocator::InsertLargeBlock(size_t size,
                                                                     size_t alignment) {
  // TODO(fxbug.dev/7180): Is there a standard way to find/specify alignment of data in
  // std::vector?.  If we had this we wouldn't need to overallocate in many
  // cases.  Another approach would be to not use vectors for large block data;
  // maybe simple malloced ptrs?
  const size_t aligned_size = size + alignment;
  return large_blocks_.emplace(large_blocks_.end(), aligned_size);
}

BlockAllocator::BlockList::iterator BlockAllocator::ObtainNextFixedSizeBlock() {
  FX_DCHECK(current_fixed_size_block_ != fixed_size_blocks_.end());
  if (++current_fixed_size_block_ == fixed_size_blocks_.end()) {
    // No next block was available, so allocate another one.
    current_fixed_size_block_ =
        fixed_size_blocks_.emplace(current_fixed_size_block_, fixed_size_block_size_);
  }
  return current_fixed_size_block_;
}

void* BlockAllocator::AllocateFromBlock(BlockList::iterator it, size_t size, size_t alignment) {
  Block& block = *it;
  uint8_t* next = AlignedToNext(block.current_ptr, alignment);
  uint8_t* end_of_next = next + size;
  if (end_of_next <= block.end) {
    // Enough free space for allocation.
    block.current_ptr = end_of_next;
    return next;
  }
  // Not enough free space for allocation;
  return nullptr;
}

BlockAllocator::Block::Block(size_t size)
    : bytes(size), current_ptr(bytes.data()), start(current_ptr), end(start + size) {}

}  // namespace escher
