// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trivial-allocator/basic-leaky-allocator.h>
#include <lib/trivial-allocator/single-heap-allocator.h>

#include <zxtest/zxtest.h>

namespace {

TEST(TrivialAllocatorTests, BasicLeakyAllocator) {
  std::byte backing_data[128];
  trivial_allocator::SingleHeapAllocator backing_allocator({backing_data});
  trivial_allocator::BasicLeakyAllocator allocator(backing_allocator);

  void *ptr = allocator.allocate(16);
  ASSERT_NOT_NULL(ptr);

  // We should be able to return the one chunk and recover the space each time.
  for (int i = 0; i < 100; ++i) {
    allocator.deallocate(ptr);
    ptr = allocator.allocate(16);
    ASSERT_NOT_NULL(ptr, "attempt %d", i);
  }
  allocator.deallocate(ptr);

  ptr = allocator.allocate(32, 16);
  EXPECT_NOT_NULL(ptr);
  EXPECT_EQ(0, reinterpret_cast<uintptr_t>(ptr) % 16);
  allocator.deallocate(ptr);

  for (int i = 0; i < 128 / 32; ++i) {
    ptr = allocator.allocate(32);
    EXPECT_NOT_NULL(ptr, "attempt %d", i);
  }
  ptr = allocator.allocate(32);
  EXPECT_NULL(ptr);

  // No-op.
  allocator.deallocate(nullptr);
}

}  // namespace
