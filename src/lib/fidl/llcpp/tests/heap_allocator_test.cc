// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/heap_allocator.h>
#include <lib/fidl/llcpp/tracking_ptr.h>

#include <cstdint>
#include <list>

#include <gtest/gtest.h>

template <size_t NBytes>
struct DestructCounterBuffer {
  DestructCounterBuffer() : count_(nullptr) {}
  DestructCounterBuffer(uint32_t* count) : count_(count) { *count_ = 0; }
  ~DestructCounterBuffer() {
    if (count_)
      ++*count_;
  }
  uint32_t* count_ = nullptr;
  uint8_t buf_[NBytes];
};

// This test can be removed whenever.  It's just a nop change to a CL to invalidate CQ results after
// CQ got confused.
TEST(HeapAllocator, NopTest) { fidl::HeapAllocator heap_allocator; }

TEST(HeapAllocator, AllocateWorksAndDestructHappensWhenExpected) {
  constexpr uint32_t kAllocationCount = 64;
  constexpr size_t kSizePerAllocation = 1024;
  uint32_t destruct_counter = 0;
  using Buffer = DestructCounterBuffer<kSizePerAllocation>;
  std::list<fidl::tracking_ptr<Buffer>> allocations;
  {  // scope allocator
    fidl::HeapAllocator allocator;
    for (uint32_t i = 0; i < kAllocationCount; ++i) {
      auto ptr = allocator.make<Buffer>(&destruct_counter);
      EXPECT_TRUE(ptr.get() != nullptr);
      // Check that the pointer does not point to within the allocator, since this would clearly
      // violate HeapAllocator's promise that all allocations can potentially out-live the
      // HeapAllocator instance.  The destruct_counter is also checked below.
      EXPECT_TRUE((reinterpret_cast<std::uintptr_t>(ptr.get()) <
                   reinterpret_cast<std::uintptr_t>(&allocator)) ||
                  (reinterpret_cast<std::uintptr_t>(ptr.get()) >
                   reinterpret_cast<std::uintptr_t>(&allocator + sizeof(allocator))));
      allocations.push_back(std::move(ptr));
    }
    // Nothing destructed yet.
    EXPECT_EQ(0u, destruct_counter);
    allocations.pop_front();
    // 1 destructed explicitly despite allocator not out of scope yet.
    EXPECT_EQ(1u, destruct_counter);
  }  // ~allocator
  // ~allocator had no effect on destruct_counter, because each HeapAllocator allocation is a
  // managed tracking_ptr that is guaranteed to out-live the HeapAllocator as long as the
  // tracking_ptr is still alive.
  EXPECT_EQ(1u, destruct_counter);
  // explicitly destruct the rest
  allocations.clear();
  EXPECT_EQ(kAllocationCount, destruct_counter);
}

TEST(HeapAllocator, ArrayAllocateWorks) {
  fidl::HeapAllocator allocator;
  auto ptr = allocator.make<DestructCounterBuffer<1024>[]>(16);
  EXPECT_TRUE(ptr.get() != nullptr);
}

TEST(HeapAllocator, ArrayAllocateCount1Works) {
  fidl::HeapAllocator allocator;
  auto ptr = allocator.make<DestructCounterBuffer<1024>[]>(1);
  EXPECT_TRUE(ptr.get() != nullptr);
}
