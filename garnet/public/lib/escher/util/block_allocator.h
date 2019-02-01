// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_BLOCK_ALLOCATOR_H_
#define LIB_ESCHER_UTIL_BLOCK_ALLOCATOR_H_

#include <list>
#include <vector>

#include "lib/escher/util/system_values.h"

namespace escher {

// A BlockAllocator allocates raw CPU data from fixed-size blocks.  To minimize
// overhead, allocations are not freed individually; the Reset() method frees
// all allocations at once.  Large allocations may exceed the size of a block;
// these are allocated separately (although still freed at the same time).
class BlockAllocator {
 public:
  explicit BlockAllocator(size_t fixed_size_block_size = 256 * 1024);
  BlockAllocator(BlockAllocator&& other) = default;

  void* Allocate(size_t size, size_t alignment = ESCHER_CACHE_LINE_SIZE);

  template <typename T>
  T* Allocate() {
    static_assert(std::is_trivially_destructible<T>::value,
                  "Type must be trivially destructible.");
    return static_cast<T*>(Allocate(sizeof(T), alignof(T)));
  }

  template <typename T>
  T* AllocateMany(size_t count) {
    static_assert(std::is_trivially_destructible<T>::value,
                  "Type must be trivially destructible.");
    static_assert(sizeof(T) % alignof(T) == 0,
                  "sizeof type must be multiple of alignof type.");
    return static_cast<T*>(Allocate(count * sizeof(T), alignof(T)));
  }

  // Invalidates all previously-allocated pointers.  Large blocks are freed, and
  // fixed-size blocks are made available for reuse.
  void Reset();

 private:
  struct Block {
    std::vector<uint8_t> bytes;
    uint8_t* current_ptr;
    uint8_t* start;
    uint8_t* end;

    // Constructor.  Resizes |bytes| to proper size, and sets |start|, |end|,
    // and |current_ptr| accordingly.
    Block(size_t size);

    void Reset() { current_ptr = start; }
  };
  using BlockList = std::list<Block>;

  BlockList::iterator InsertLargeBlock(size_t size, size_t alignment);
  BlockList::iterator ObtainNextFixedSizeBlock();
  void* AllocateFromBlock(BlockList::iterator it, size_t size,
                          size_t alignment);

  const size_t fixed_size_block_size_;
  BlockList fixed_size_blocks_;
  BlockList::iterator current_fixed_size_block_;
  BlockList large_blocks_;

 public:
  // For debugging/testing.
  const BlockList& fixed_size_blocks() const { return fixed_size_blocks_; }
  const BlockList& large_blocks() const { return large_blocks_; }
  const Block& current_fixed_size_block() const {
    return *current_fixed_size_block_;
  }
};

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_BLOCK_ALLOCATOR_H_
