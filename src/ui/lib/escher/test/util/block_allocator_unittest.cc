// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/block_allocator.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/util/align.h"

namespace {
using namespace escher;

TEST(BlockAllocator, InitialCounts) {
  BlockAllocator allocator;
  EXPECT_EQ(1u, allocator.fixed_size_blocks().size());
  EXPECT_EQ(0u, allocator.large_blocks().size());
  EXPECT_EQ(allocator.current_fixed_size_block().current_ptr,
            allocator.current_fixed_size_block().start);
  EXPECT_LT(allocator.current_fixed_size_block().current_ptr,
            allocator.current_fixed_size_block().end);

  // Other tests will rely on block memory being at least 4-byte aligned.
  ASSERT_EQ(0u, reinterpret_cast<size_t>(allocator.current_fixed_size_block().current_ptr) %
                    alignof(uint32_t));
}

TEST(BlockAllocator, SmallAllocations) {
  constexpr size_t kFixedBlockSize = 128;
  BlockAllocator allocator(kFixedBlockSize);

  auto& current_block = allocator.current_fixed_size_block();

  uint32_t* val0 = allocator.Allocate<uint32_t>();
  uint32_t* val1 = allocator.Allocate<uint32_t>();
  EXPECT_EQ(val1, val0 + 1);
  EXPECT_EQ(8u, current_block.current_ptr - current_block.start);

  // If we allocate N == 1-4 additional uint8_t in the middle, this results in
  // 4-N bytes of padding to meet the alignment requirements for the next
  // uint32_t.
  for (size_t i = 1; i <= 4; ++i) {
    val0 = allocator.Allocate<uint32_t>();
    EXPECT_EQ(val0, val1 + 1);
    allocator.AllocateMany<uint8_t>(i);
    val1 = allocator.Allocate<uint32_t>();
    EXPECT_EQ(val1, val0 + 2);
  }
  // 4 loop iterations, 12 bytes allocated per iteration.  Adding this to the
  // previous total of 8 bytes gives a total of 44 bytes allocated.
  EXPECT_EQ(1u, allocator.fixed_size_blocks().size());
  EXPECT_EQ(56u, current_block.current_ptr - current_block.start);

  for (size_t i = 5; i <= 8; ++i) {
    val0 = allocator.Allocate<uint32_t>();
    EXPECT_EQ(val0, val1 + 1);
    allocator.AllocateMany<uint8_t>(i);
    val1 = allocator.Allocate<uint32_t>();
    EXPECT_EQ(val1, val0 + 3);
  }
  // 4 loop iterations, 16 bytes allocated per iteration.  Adding this to the
  // previous total of 56 bytes gives a total of 44 bytes allocated.
  EXPECT_EQ(1u, allocator.fixed_size_blocks().size());
  EXPECT_EQ(120u, current_block.current_ptr - current_block.start);

  // There is room to allocate 2 more uint32_t in the block before an additional
  // block is required.
  allocator.AllocateMany<uint32_t>(2);
  EXPECT_EQ(1u, allocator.fixed_size_blocks().size());

  // No free space is left in the current block.  Allocating a single uint8_t
  // results in a new block being allocated.
  uint8_t* val2 = allocator.Allocate<uint8_t>();
  EXPECT_EQ(2u, allocator.fixed_size_blocks().size());

  // Resetting the allocator will reuse the existing blocks.  After allocating
  // 128 bytes (which is equal to kFixedBlockSize), the next byte will be
  // identical to val2.  Note: the 128 bytes must be allocated in smaller
  // chunks, else they would be treated as a large block allocation.
  allocator.Reset();
  for (size_t i = 0; i < 128; ++i) {
    EXPECT_NE(val2, allocator.Allocate<uint8_t>());
  }
  EXPECT_EQ(val2, allocator.Allocate<uint8_t>());

  EXPECT_EQ(0u, allocator.large_blocks().size());
}

TEST(BlockAllocator, LargeAllocations) {
  constexpr size_t kFixedBlockSize = 128;
  constexpr size_t kLargestFixedSizeBlockAllocation = kFixedBlockSize / 4;
  BlockAllocator allocator(kFixedBlockSize);

  // Anything up to 1/4 of the fixed block size is treated as a regular (small)
  // allocation.
  EXPECT_NE(nullptr, allocator.Allocate(kLargestFixedSizeBlockAllocation, 4));
  EXPECT_NE(nullptr, allocator.Allocate(kLargestFixedSizeBlockAllocation, 4));
  EXPECT_NE(nullptr, allocator.Allocate(kLargestFixedSizeBlockAllocation, 4));
  EXPECT_NE(nullptr, allocator.Allocate(kLargestFixedSizeBlockAllocation, 4));
  EXPECT_EQ(1u, allocator.fixed_size_blocks().size());
  EXPECT_EQ(0u, allocator.large_blocks().size());

  // One more byte will overflow the first fixed-size block.
  allocator.Allocate(1, 1);
  EXPECT_EQ(2u, allocator.fixed_size_blocks().size());
  EXPECT_EQ(0u, allocator.large_blocks().size());

  // Anything larger than kLargestFixedSizeBlockAllocation will be treated as a
  // large allocation, which gets its own block.
  EXPECT_NE(nullptr, allocator.Allocate(kLargestFixedSizeBlockAllocation + 1, 4));
  EXPECT_NE(nullptr, allocator.Allocate(kFixedBlockSize / 3, 4));
  EXPECT_NE(nullptr, allocator.Allocate(kFixedBlockSize / 2, 4));
  EXPECT_NE(nullptr, allocator.Allocate(kFixedBlockSize, 4));
  EXPECT_NE(nullptr, allocator.Allocate(kFixedBlockSize * 2, 4));
  EXPECT_EQ(2u, allocator.fixed_size_blocks().size());
  EXPECT_EQ(5u, allocator.large_blocks().size());

  // Resetting the allocator frees all of the large blocks.
  allocator.Reset();
  EXPECT_EQ(0u, allocator.large_blocks().size());

  // AllocateMany() allocates space contiguously, so although allocating one
  // 32-byte struct will be treated as a small allocation, allocating two will
  // use a large block.
  struct ThirtyTwoBytes {
    uint8_t bytes[32];
  };
  static_assert(sizeof(ThirtyTwoBytes) == 32, "Expecting 32 bytes.");
  EXPECT_NE(nullptr, allocator.Allocate<ThirtyTwoBytes>());
  EXPECT_EQ(0u, allocator.large_blocks().size());
  EXPECT_NE(nullptr, allocator.AllocateMany<ThirtyTwoBytes>(2));
  EXPECT_EQ(1u, allocator.large_blocks().size());
}

}  // namespace
