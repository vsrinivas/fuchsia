// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/span.h>
#include <lib/trivial-allocator/single-heap-allocator.h>

#include <gtest/gtest.h>

namespace {

TEST(TrivialAllocatorTests, SingleHeapAllocator) {
  alignas(uint64_t) std::byte aligned_buffer[128];

  trivial_allocator::SingleHeapAllocator aligned_heap{cpp20::span(aligned_buffer)};

  size_t size = sizeof(aligned_buffer) + 1;
  auto allocation = aligned_heap(size, 1);
  EXPECT_FALSE(allocation);

  size = sizeof(aligned_buffer) - 1;
  allocation = aligned_heap(size, 1);
  EXPECT_TRUE(allocation);
  EXPECT_EQ(sizeof(aligned_buffer), size);
  EXPECT_EQ(aligned_buffer, allocation.get());

  size = 1;
  auto second_allocation = aligned_heap(size, 1);
  EXPECT_FALSE(second_allocation);

  trivial_allocator::SingleHeapAllocator misaligned_heap{cpp20::span{
      &aligned_buffer[1],
      sizeof(aligned_buffer) - 1,
  }};

  size = sizeof(aligned_buffer);
  auto allocation_from_misaligned = misaligned_heap(size, 8);
  EXPECT_FALSE(allocation_from_misaligned);

  size = sizeof(aligned_buffer) - 8;
  allocation_from_misaligned = misaligned_heap(size, 8);
  EXPECT_TRUE(allocation_from_misaligned);
  EXPECT_EQ(sizeof(aligned_buffer) - 1, size);
  EXPECT_EQ(&aligned_buffer[1], allocation_from_misaligned.get());

  size = 1;
  auto second_allocation_from_misaligned = misaligned_heap(size, 8);
  EXPECT_FALSE(second_allocation_from_misaligned);
}

}  // namespace
