// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/buffer_then_heap_allocator.h>
#include <lib/fidl/llcpp/failover_allocator.h>

#include "gtest/gtest.h"

template <size_t NBytes>
struct DestructCounterBuffer {
  DestructCounterBuffer() : count(nullptr) {}
  DestructCounterBuffer(int* count) : count(count) {}
  ~DestructCounterBuffer() { ++*count; }
  int* count;
  uint8_t buf_[NBytes];
};

TEST(FailoverAllocator, InnerAllocatorTest) {
  int destructCount = 0;
  {
    fidl::FailoverHeapAllocator<fidl::UnsafeBufferAllocator<2048>> allocator;
    {
      auto destruct_counter = allocator.make<DestructCounterBuffer<100>>(&destructCount);

      // Check that the pointer points to the buffer within the allocator.
      EXPECT_TRUE((reinterpret_cast<std::uintptr_t>(destruct_counter.get()) >=
                   reinterpret_cast<std::uintptr_t>(&allocator)) &&
                  (reinterpret_cast<std::uintptr_t>(destruct_counter.get()) <=
                   reinterpret_cast<std::uintptr_t>(&allocator + sizeof(allocator))));
    }
    EXPECT_EQ(destructCount, 0);
  }
  EXPECT_EQ(destructCount, 1);
}

TEST(FailoverAllocator, FailoverAllocationTest) {
  int destructCount = 0;
  {
    fidl::tracking_ptr<DestructCounterBuffer<2048>> destruct_counter;
    {
      fidl::FailoverHeapAllocator<fidl::UnsafeBufferAllocator<10>> allocator;
      // Make this big enough so it has to be heap allocated.
      destruct_counter = allocator.make<DestructCounterBuffer<2048>>(&destructCount);

      // Check that the pointer does not point to the buffer within the allocator.
      EXPECT_TRUE((reinterpret_cast<std::uintptr_t>(destruct_counter.get()) <
                   reinterpret_cast<std::uintptr_t>(&allocator)) ||
                  (reinterpret_cast<std::uintptr_t>(destruct_counter.get()) >
                   reinterpret_cast<std::uintptr_t>(&allocator + sizeof(allocator))));

      EXPECT_EQ(destructCount, 0);
    }
    // Failover is the heap so it is still available until failover_buffer goes out of scope.
    EXPECT_EQ(destructCount, 0);
  }
  EXPECT_EQ(destructCount, 1);
}

TEST(FailoverAllocator, AccessInnerAllocator) {
  fidl::FailoverHeapAllocator<fidl::UnsafeBufferAllocator<2048>> allocator;
  allocator.inner_allocator().make<uint8_t>(1);
}

TEST(FailoverAllocator, FailoverArrayAllocation) {
  constexpr size_t kArraySize = 1000;
  fidl::FailoverHeapAllocator<fidl::UnsafeBufferAllocator<10>> allocator;
  auto array = allocator.make<uint64_t[]>(kArraySize);
  // Write to each so ASAN can pick up on bad accesses.
  for (size_t i = 0; i < kArraySize; i++) {
    array[i] = i;
  }
}

TEST(FailoverAllocator, FailoverSingleArrayAllocation) {
  fidl::FailoverHeapAllocator<fidl::UnsafeBufferAllocator<0>> allocator;
  auto array = allocator.make<uint64_t[]>(1);
  array[0] = 0xabc;
}
