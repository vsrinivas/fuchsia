// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_CPP_VMO_HEAP_H_
#define LIB_INSPECT_CPP_VMO_HEAP_H_

#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/limits.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>

namespace inspect {
namespace internal {

// A buddy-allocated heap of blocks stored in a VMO.
//
// |Heap| supports Allocate and Free operations to
// allocate memory stored in a VMO. |Heap| allocations
// touch a new page of the VMO (up to its capacity) only
// when necessary to satisfy the allocation. This ensures
// the VMO's default behavior of mapping all untouched
// pages to a single physical "zero" page results in the
// heap using the least amount of physical memory to
// satisfy requests.
//
// This class is not thread safe.
class Heap final {
 public:
  // Create a new heap that allocates out of the given |vmo|.
  //
  // The VMO must not be zero-sized.
  explicit Heap(zx::vmo vmo);
  ~Heap();

  // Gets a reference to the underlying VMO.
  const zx::vmo& GetVmo() const;

  // Allocate a |BlockIndex| out of the heap that can contain at least |min_size| bytes.
  // Allocating a block larger that |kMaxOrderSize| bytes will fail.
  //
  // Returns ZX_OK on success or an error on failure.
  // |out_block| will be set to the allocated block index on success only.
  //
  // Warning: It is an error to destroy the heap without freeing all blocks first.
  zx_status_t Allocate(size_t min_size, BlockIndex* out_block);

  // Free a |BlockIndex| allocated from this heap.
  void Free(BlockIndex block_index);

  // Get a pointer to the |Block| for the given |Block|.
  Block* GetBlock(BlockIndex block) const {
    return reinterpret_cast<Block*>((reinterpret_cast<uint8_t*>(buffer_addr_)) +
                                    block * kMinOrderSize);
  }

  // Return a pointer to the data buffer.
  const uint8_t* data() const { return reinterpret_cast<uint8_t*>(buffer_addr_); }

  // Return the current usable size of the VMO.
  size_t size() const { return cur_size_; }

  // Return the maximum size of the VMO.
  size_t maximum_size() const { return max_size_; }

 private:
  // Returns true if the given block is free and of the expected order.
  inline bool IsFreeBlock(BlockIndex block, size_t expected_order) const;

  bool SplitBlock(BlockIndex block);
  bool RemoveFree(BlockIndex block);
  zx_status_t Extend(size_t new_size);

  zx::vmo vmo_;
  size_t cur_size_ = 0;
  size_t max_size_ = 0;
  uintptr_t buffer_addr_ = 0;
  BlockIndex free_blocks_[8] = {};

  // Keep track of the number of allocated blocks to assert that they are all freed
  // before the heap is destroyed.
  size_t num_allocated_blocks_ = 0;
};

bool Heap::IsFreeBlock(BlockIndex block, size_t expected_order) const {
  ZX_DEBUG_ASSERT_MSG(block < IndexForOffset(cur_size_), "Block out of bounds");
  if (block >= cur_size_ / kMinOrderSize) {
    return false;
  }
  auto* b = GetBlock(block);
  return GetType(b) == BlockType::kFree && GetOrder(b) == expected_order;
}

}  // namespace internal
}  // namespace inspect

#endif  // LIB_INSPECT_CPP_VMO_HEAP_H_
