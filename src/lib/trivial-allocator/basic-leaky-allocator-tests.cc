// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trivial-allocator/basic-leaky-allocator.h>
#include <lib/trivial-allocator/single-heap-allocator.h>

#include <gtest/gtest.h>

namespace {

TEST(TrivialAllocatorTests, BasicLeakyAllocator) {
  alignas(16) std::byte backing_data[128];
  trivial_allocator::SingleHeapAllocator backing_allocator({backing_data});
  trivial_allocator::BasicLeakyAllocator allocator(backing_allocator);

  void* ptr = allocator.allocate(16);
  ASSERT_TRUE(ptr);

  // We should be able to return the one chunk and recover the space each time.
  for (int i = 0; i < 100; ++i) {
    allocator.deallocate(ptr);
    ptr = allocator.allocate(16);
    ASSERT_TRUE(ptr) << "attempt " << i;
  }
  allocator.deallocate(ptr);

  ptr = allocator.allocate(32, 16);
  EXPECT_TRUE(ptr);
  EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(ptr) % 16);
  allocator.deallocate(ptr);

  for (int i = 0; i < 128 / 32; ++i) {
    ptr = allocator.allocate(32);
    EXPECT_TRUE(ptr) << "attempt " << i;
  }
  ptr = allocator.allocate(32);
  EXPECT_FALSE(ptr);

  // No-op.
  allocator.deallocate(nullptr);

  // Since backing_data is aligned to 16, 1 element into it is definitely
  // misaligned.
  cpp20::span misaligned_backing_data = cpp20::span(backing_data).subspan(1);
  trivial_allocator::SingleHeapAllocator misaligned_backing_allocator(misaligned_backing_data);
  trivial_allocator::BasicLeakyAllocator misaligned_allocator(misaligned_backing_allocator);

  // Allocating with no alignment requirement should be happy with the first
  // possible chunk.
  ptr = misaligned_allocator.allocate(1, 1);
  EXPECT_TRUE(ptr);
  EXPECT_EQ(ptr, misaligned_backing_data.data());

  auto expect_aligned_to = [&backing_data](void* ptr, size_t alignment) {
    void* align_check = ptr;
    size_t align_avail = &backing_data[sizeof(backing_data)] - static_cast<std::byte*>(ptr);
    EXPECT_EQ(std::align(alignment, alignment, align_check, align_avail), ptr);
  };

  // Allocating with a large required alignment should still work.
  ptr = misaligned_allocator.allocate(32, 32);
  EXPECT_TRUE(ptr);
  expect_aligned_to(ptr, 32);

  const std::array align_test_chunks = {
      // First, a well-aligned chunk only big enough for the first allocation.
      cpp20::span(backing_data).subspan(0, 16),
      // Second, a misaligned chunk big enough for the second allocation.
      cpp20::span(backing_data).subspan(17, 31),
      // Third, a misaligned chunk big enough for the third allocation but not
      // big enough to make it aligned.
      cpp20::span(backing_data).subspan(49, 16),
      // Finally, an aligned chunk that's just big enough for pessimistic
      // overalignment.
      cpp20::span(backing_data).subspan(80, 31),
  };
  auto align_test_heap = [chunks = cpp20::span<const cpp20::span<std::byte>>(align_test_chunks)](
                             size_t& size, size_t alignment) mutable {
    cpp20::span<std::byte> chunk;
    if (!chunks.empty()) {
      chunk = chunks.front();
      chunks = chunks.subspan(1);
    }
    return trivial_allocator::SingleHeapAllocator(chunk)(size, alignment);
  };
  trivial_allocator::BasicLeakyAllocator align_test_allocator(align_test_heap);

  // First allocation consumes first chunk.
  ptr = align_test_allocator.allocate(16, 16);
  EXPECT_TRUE(ptr);
  expect_aligned_to(ptr, 16);

  // Second allocation consumes second chunk.
  ptr = align_test_allocator.allocate(16, 16);
  EXPECT_TRUE(ptr);
  expect_aligned_to(ptr, 16);

  // Third allocation skips remainder of third chunk and uses fourth.
  ptr = align_test_allocator.allocate(16, 16);
  EXPECT_TRUE(ptr);
  expect_aligned_to(ptr, 16);

  // Fourth allocation consumes remainder of third chunk.
  ptr = align_test_allocator.allocate(15, 1);
  EXPECT_TRUE(ptr);

  // Should now be fresh out of chunks.
  ptr = align_test_allocator.allocate(1, 1);
  EXPECT_FALSE(ptr);
}

}  // namespace
