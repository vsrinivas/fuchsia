// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/buffer_allocator.h>

#include "gtest/gtest.h"

TEST(BufferAllocator, MultipleArgumentMake) {
  struct A {
    A(int64_t x, bool y) : x(x), y(y) {}
    int64_t x;
    bool y;
  };
  fidl::BufferAllocator<2048> allocator;
  fidl::tracking_ptr<A> ptr = allocator.make<A>(1, true);
  EXPECT_EQ(ptr->x, 1);
  EXPECT_EQ(ptr->y, true);
}

TEST(BufferAllocator, AllocationLayout) {
  fidl::BufferAllocator<2048> allocator;
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

TEST(BufferAllocator, SingleItemDestructor) {
  int destructCountA = 0;
  int destructCountB = 0;
  int destructCountC = 0;
  {
    fidl::BufferAllocator<2048> allocator;
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

TEST(BufferAllocator, ArrayDestructor) {
  constexpr int n = 3;
  int destructCounts[n] = {};
  {
    fidl::BufferAllocator<2048> allocator;
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

TEST(BufferAllocator, PrimitiveEightBytesEach) {
  // Primitives will each use 8 bytes because allocations maintain FIDL_ALIGNMENT.
  fidl::BufferAllocator<64> allocator;
  uint8_t* last_ptr = nullptr;
  for (int i = 0; i < 8; i++) {
    fidl::tracking_ptr<uint16_t> ptr = allocator.make<uint16_t>();

    uint8_t* ptr_as_byte = reinterpret_cast<uint8_t*>(ptr.get());
    EXPECT_GE(ptr_as_byte - last_ptr, 8);
    last_ptr = ptr_as_byte;
  }
}

TEST(BufferAllocator, PrimitiveArrayFullSpace) {
  // Primitives using at least 2 byte alignment should be able to allocate the
  // full space. There should be no metadata.
  // Currently (in the name of keeping the allocator interface simple), there is
  // no way to verify the internal allocator state, in that all 32 bytes were
  // consumed.
  fidl::BufferAllocator<32> allocator;
  fidl::tracking_ptr<uint16_t[]> ptr = allocator.make<uint16_t[]>(16);
  for (int i = 0; i < 16; i++)
    EXPECT_EQ(ptr[i], 0);
}

TEST(BufferAllocator, EmptyAllocator) {
  // In some implementations, it might be possible for uninitialized fields to trigger bad behavior
  // for instance, uninitialized destructor metadata could be misinterpreted.
  fidl::BufferAllocator<2048> allocator;
}
