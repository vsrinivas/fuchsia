// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/buffer_then_heap_allocator.h>

#include <list>

#include <gtest/gtest.h>

TEST(BufferThenHeapAllocator, MultipleArgumentMake) {
  struct A {
    A(int64_t x, bool y) : x(x), y(y) {}
    int64_t x;
    bool y;
  };
  fidl::BufferThenHeapAllocator<2048> allocator;
  fidl::tracking_ptr<A> ptr = allocator.make<A>(1, true);
  EXPECT_EQ(ptr->x, 1);
  EXPECT_EQ(ptr->y, true);
}

TEST(BufferThenHeapAllocator, AllocationLayout) {
  fidl::BufferThenHeapAllocator<2048> allocator;
  fidl::tracking_ptr<uint8_t> ptr1 = allocator.make<uint8_t>();
  fidl::tracking_ptr<uint8_t> ptr2 = allocator.make<uint8_t>();
  fidl::tracking_ptr<uint64_t[]> ptr3 = allocator.make<uint64_t[]>(2);
  fidl::tracking_ptr<uint16_t> ptr4 = allocator.make<uint16_t>();

  // Alignment.
  EXPECT_TRUE(reinterpret_cast<uintptr_t>(ptr1.get()) % FIDL_ALIGNMENT == 0);
  EXPECT_TRUE(reinterpret_cast<uintptr_t>(ptr2.get()) % FIDL_ALIGNMENT == 0);
  EXPECT_TRUE(reinterpret_cast<uintptr_t>(ptr3.get()) % FIDL_ALIGNMENT == 0);
  EXPECT_TRUE(reinterpret_cast<uintptr_t>(ptr4.get()) % FIDL_ALIGNMENT == 0);

  // Ensure objects don't overlap.
  // The +1 is to get to the end of the object.
  EXPECT_LE(ptr1.get() + 1, ptr2.get());
  EXPECT_LE(ptr2.get() + 1, reinterpret_cast<uint8_t*>(ptr3.get()));
  EXPECT_LE(ptr3.get() + 1, reinterpret_cast<uint64_t*>(ptr4.get()));
}

struct DestructCounter {
  DestructCounter() : count(nullptr) {}
  DestructCounter(int* count) : count(count) {}
  ~DestructCounter() { ++*count; }
  int* count;
};

TEST(BufferThenHeapAllocator, SingleItemDestructor) {
  int destructCountA = 0;
  int destructCountB = 0;
  int destructCountC = 0;
  {
    fidl::BufferThenHeapAllocator<2048> allocator;
    {
      allocator.make<DestructCounter>(&destructCountA);
      allocator.make<DestructCounter>(&destructCountB);
      allocator.make<DestructCounter>(&destructCountC);
    }
    EXPECT_EQ(destructCountA, 0);
    EXPECT_EQ(destructCountB, 0);
    EXPECT_EQ(destructCountC, 0);
  }
  EXPECT_EQ(destructCountA, 1);
  EXPECT_EQ(destructCountB, 1);
  EXPECT_EQ(destructCountC, 1);
}

TEST(BufferThenHeapAllocator, ResetDestructor) {
  int destructCountA = 0;
  int destructCountB = 0;
  int destructCountC = 0;

  {
    fidl::BufferThenHeapAllocator<2048> allocator;

    allocator.make<DestructCounter>(&destructCountA);
    allocator.make<DestructCounter>(&destructCountB);
    allocator.make<DestructCounter>(&destructCountC);

    EXPECT_EQ(destructCountA, 0);
    EXPECT_EQ(destructCountB, 0);
    EXPECT_EQ(destructCountC, 0);

    allocator.inner_allocator().reset();

    EXPECT_EQ(destructCountA, 1);
    EXPECT_EQ(destructCountB, 1);
    EXPECT_EQ(destructCountC, 1);

    allocator.make<DestructCounter>(&destructCountA);
    allocator.make<DestructCounter>(&destructCountB);
    allocator.make<DestructCounter>(&destructCountC);

    EXPECT_EQ(destructCountA, 1);
    EXPECT_EQ(destructCountB, 1);
    EXPECT_EQ(destructCountC, 1);
  }
  EXPECT_EQ(destructCountA, 2);
  EXPECT_EQ(destructCountB, 2);
  EXPECT_EQ(destructCountC, 2);
}

TEST(BufferThenHeapAllocator, ArrayDestructor) {
  constexpr int n = 3;
  int destructCounts[n] = {};
  {
    fidl::BufferThenHeapAllocator<2048> allocator;
    {
      fidl::tracking_ptr<DestructCounter[]> ptr = allocator.make<DestructCounter[]>(n);
      for (int i = 0; i < n; i++) {
        ptr[i].count = &destructCounts[i];
      }
    }
    for (int i = 0; i < n; i++) {
      EXPECT_EQ(destructCounts[i], 0);
    }
  }
  for (int i = 0; i < n; i++) {
    EXPECT_EQ(destructCounts[i], 1);
  }
}

TEST(BufferThenHeapAllocator, PrimitiveEightBytesEach) {
  // Primitives will each use 8 bytes because allocations maintain FIDL_ALIGNMENT.
  fidl::BufferThenHeapAllocator<64> allocator;
  uint8_t* last_ptr = nullptr;
  for (int i = 0; i < 8; i++) {
    fidl::tracking_ptr<uint16_t> ptr = allocator.make<uint16_t>();

    uint8_t* ptr_as_byte = reinterpret_cast<uint8_t*>(ptr.get());
    EXPECT_GE(ptr_as_byte - last_ptr, 8);
    last_ptr = ptr_as_byte;
  }
}

TEST(BufferThenHeapAllocator, PrimitiveArrayFullSpace) {
  // Primitives using at least 2 byte alignment should be able to allocate the
  // full space. There should be no metadata.
  // Currently (in the name of keeping the allocator interface simple), there is
  // no way to verify the internal allocator state, in that all 32 bytes were
  // consumed.
  fidl::BufferThenHeapAllocator<32> allocator;
  fidl::tracking_ptr<uint16_t[]> ptr = allocator.make<uint16_t[]>(16);
  for (int i = 0; i < 16; i++)
    EXPECT_EQ(ptr[i], 0);
}

TEST(BufferThenHeapAllocator, EmptyAllocator) {
  // In some implementations, it might be possible for uninitialized fields to trigger bad behavior
  // for instance, uninitialized destructor metadata could be misinterpreted.
  fidl::BufferThenHeapAllocator<2048> allocator;
}

template <size_t NBytes>
struct DestructCounterBuffer {
  DestructCounterBuffer() : count_(nullptr) {}
  DestructCounterBuffer(uint32_t* count) : count_(count) { *count_ = 0; }
  ~DestructCounterBuffer() {
    if (count_) {
      ++*count_;
    }
  }
  uint32_t* count_ = nullptr;
  uint8_t buf_[NBytes];
};

TEST(BufferThenHeapAllocator, TooSmallAllocatorWorksAnyway) {
  constexpr uint32_t kAllocationCount = 64;
  constexpr size_t kBufferThenHeapAllocatorSize = 1;
  constexpr size_t kPerAllocationSize = 1024;
  uint32_t destruction_count = 0;
  using Buffer = DestructCounterBuffer<kPerAllocationSize>;
  std::list<fidl::tracking_ptr<Buffer>> allocations;
  {  // scope allocator
    fidl::BufferThenHeapAllocator<kBufferThenHeapAllocatorSize> allocator;
    for (uint32_t i = 0; i < kAllocationCount; ++i) {
      auto ptr = allocator.make<Buffer>(&destruction_count);
      EXPECT_TRUE(ptr.get() != nullptr);
      allocations.push_back(std::move(ptr));
    }
  }  // ~allocator
  // None of the items are destructed yet because they were all allocated on the heap.
  EXPECT_EQ(0u, destruction_count);
  // Now destruct and deallocate all the allocations.
  allocations.clear();
  EXPECT_EQ(kAllocationCount, destruction_count);
}

TEST(BufferThenHeapAllocator, InternalThenHeapFallback) {
  constexpr uint32_t kAllocationCount = 64;
  constexpr size_t kBufferThenHeapAllocatorSize = 1024;
  constexpr size_t kPerAllocationSize = 768;
  uint32_t destruction_count = 0;
  using Buffer = DestructCounterBuffer<kPerAllocationSize>;
  std::list<fidl::tracking_ptr<Buffer>> allocations;
  {  // scope allocator
    fidl::BufferThenHeapAllocator<kBufferThenHeapAllocatorSize> allocator;
    for (uint32_t i = 0; i < kAllocationCount; ++i) {
      auto ptr = allocator.make<Buffer>(&destruction_count);
      EXPECT_TRUE(ptr.get() != nullptr);
      if (i == 0) {
        // Check that the pointer points to the buffer within the allocator.
        EXPECT_TRUE((reinterpret_cast<std::uintptr_t>(ptr.get()) >=
                     reinterpret_cast<std::uintptr_t>(&allocator)) &&
                    (reinterpret_cast<std::uintptr_t>(ptr.get()) <=
                     reinterpret_cast<std::uintptr_t>(&allocator + sizeof(allocator))));
      } else {
        // Check that the pointer does not point to the buffer within the allocator.
        EXPECT_TRUE((reinterpret_cast<std::uintptr_t>(ptr.get()) <
                     reinterpret_cast<std::uintptr_t>(&allocator)) ||
                    (reinterpret_cast<std::uintptr_t>(ptr.get()) >
                     reinterpret_cast<std::uintptr_t>(&allocator + sizeof(allocator))));
      }
      allocations.push_back(std::move(ptr));
    }
  }  // ~allocator
  // 1 of the items are destructed because 1 was in the allocator.
  EXPECT_EQ(1u, destruction_count);
  // The first item is an unowned_ptr_t, so it's fine that it's about to be deleted - it won't run
  // the destructor or double-free its ptr.  The fact that its presently dangling is expected as
  // allocations by a BufferThenHeapAllocator<> cannot be assumed to out-last the allocator.
  //
  // Now destruct and deallocate all the allocations.
  allocations.clear();
  EXPECT_EQ(kAllocationCount, destruction_count);
}

TEST(BufferThenHeapAllocator, InternalAllocationTest) {
  uint32_t destruct_count = 0;
  {
    fidl::BufferThenHeapAllocator<2048> allocator;
    {
      auto destruct_counter = allocator.make<DestructCounterBuffer<100>>(&destruct_count);

      // Check that the pointer points to the buffer within the allocator.
      EXPECT_TRUE((reinterpret_cast<std::uintptr_t>(destruct_counter.get()) >=
                   reinterpret_cast<std::uintptr_t>(&allocator)) &&
                  (reinterpret_cast<std::uintptr_t>(destruct_counter.get()) <=
                   reinterpret_cast<std::uintptr_t>(&allocator + sizeof(allocator))));
    }
    EXPECT_EQ(0u, destruct_count);
  }
  EXPECT_EQ(1u, destruct_count);
}

TEST(BufferThenHeapAllocator, FailoverAllocationTest) {
  uint32_t destruct_count = 0;
  {
    fidl::tracking_ptr<DestructCounterBuffer<2048>> destruct_counter;
    {
      fidl::BufferThenHeapAllocator<10> allocator;
      // Make this big enough so it has to be heap allocated.
      destruct_counter = allocator.make<DestructCounterBuffer<2048>>(&destruct_count);

      // Check that the pointer does not point to the buffer within the allocator.
      EXPECT_TRUE((reinterpret_cast<std::uintptr_t>(destruct_counter.get()) <
                   reinterpret_cast<std::uintptr_t>(&allocator)) ||
                  (reinterpret_cast<std::uintptr_t>(destruct_counter.get()) >
                   reinterpret_cast<std::uintptr_t>(&allocator + sizeof(allocator))));

      EXPECT_EQ(0u, destruct_count);
    }
    // Failover is the heap so it is still available until destruct_counter goes out of scope.
    EXPECT_EQ(0u, destruct_count);
  }
  EXPECT_EQ(1u, destruct_count);
}

TEST(BufferThenHeapAllocator, FailoverArrayAllocation) {
  constexpr size_t kArraySize = 1000;
  fidl::BufferThenHeapAllocator<10> allocator;
  auto array = allocator.make<uint64_t[]>(kArraySize);
  // Write to each so ASAN can pick up on bad accesses.
  for (size_t i = 0; i < kArraySize; i++) {
    array[i] = i;
  }
}

TEST(BufferThenHeapAllocator, FailoverSingleEntryArrayAllocation) {
  constexpr size_t kPerAllocationSize = 1024;
  fidl::BufferThenHeapAllocator<1> allocator;
  using Buffer = DestructCounterBuffer<kPerAllocationSize>;
  auto array = allocator.make<Buffer[]>(1);
  array[0].buf_[0] = 0xab;
}
