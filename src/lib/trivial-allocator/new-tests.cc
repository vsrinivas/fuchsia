// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trivial-allocator/basic-leaky-allocator.h>
#include <lib/trivial-allocator/new.h>
#include <lib/trivial-allocator/single-heap-allocator.h>

#include <zxtest/zxtest.h>

namespace {

TEST(TrivialAllocatorTests, New) {
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
    ASSERT_NOT_NULL(iptr);
    ASSERT_TRUE(ac.check());
    EXPECT_EQ(17, *iptr);
  }

  {
    fbl::AllocChecker ac;
    char* cptr = new (allocator, ac) char[11];
    ASSERT_NOT_NULL(cptr);
    ASSERT_TRUE(ac.check());
  }

  {
    fbl::AllocChecker ac;
    Aligned* aptr = new (allocator, ac) Aligned{23};
    ASSERT_NOT_NULL(aptr);
    ASSERT_TRUE(ac.check());
    EXPECT_EQ(23, aptr->data);
  }

  {
    fbl::AllocChecker ac;
    Aligned* aptr = new (allocator, ac) Aligned[2]{{1}, {2}};
    ASSERT_NOT_NULL(aptr);
    ASSERT_TRUE(ac.check());
    EXPECT_EQ(1, aptr[0].data);
    EXPECT_EQ(2, aptr[1].data);
  }

  // 4 + 11 + align to 32 + 32 + 2*32 = 128, no space left
  {
    fbl::AllocChecker ac;
    char* cptr = new (allocator, ac) char;
    EXPECT_NULL(cptr);
    EXPECT_FALSE(ac.check());
  }
}

}  // namespace
