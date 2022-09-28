// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trivial-allocator/basic-leaky-allocator.h>
#include <lib/trivial-allocator/new.h>
#include <lib/trivial-allocator/single-heap-allocator.h>

#include <gtest/gtest.h>

namespace {

TEST(TrivialAllocatorTests, StubDelete) {
  alignas(32) std::byte backing_buffer[128];
  trivial_allocator::SingleHeapAllocator backing_allocator({backing_buffer});
  trivial_allocator::BasicLeakyAllocator allocator(backing_allocator);

  struct Aligned {
    alignas(32) int data;
  };
  static_assert(alignof(Aligned) == 32);
  static_assert(sizeof(Aligned) == 32);

  {
    fbl::AllocChecker ac;
    int* iptr = new (allocator, ac) int(17);
    ASSERT_TRUE(iptr);
    ASSERT_TRUE(ac.check());
    delete iptr;
  }

  {
    fbl::AllocChecker ac;
    char* cptr = new (allocator, ac) char[11];
    ASSERT_TRUE(cptr);
    ASSERT_TRUE(ac.check());
    delete[] cptr;
  }

  {
    fbl::AllocChecker ac;
    Aligned* aptr = new (allocator, ac) Aligned{23};
    ASSERT_TRUE(aptr);
    ASSERT_TRUE(ac.check());
    delete aptr;
  }

  {
    fbl::AllocChecker ac;
    Aligned* aaptr = new (allocator, ac) Aligned[2]{{1}, {2}};
    ASSERT_TRUE(aaptr);
    ASSERT_TRUE(ac.check());
    delete[] aaptr;
  }
}

}  // namespace
