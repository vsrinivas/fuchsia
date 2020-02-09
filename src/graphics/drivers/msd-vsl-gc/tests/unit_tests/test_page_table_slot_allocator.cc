// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/graphics/drivers/msd-vsl-gc/src/page_table_slot_allocator.h"

class TestPageTableSlotAllocator {
 public:
  static void Alloc() {
    PageTableSlotAllocator allocator(2);
    uint32_t index0;
    EXPECT_TRUE(allocator.Alloc(&index0));
    EXPECT_LT(index0, allocator.size());
    uint32_t index1;
    EXPECT_TRUE(allocator.Alloc(&index1));
    EXPECT_LT(index1, allocator.size());
    EXPECT_NE(index0, index1);
    uint32_t dummy;
    EXPECT_FALSE(allocator.Alloc(&dummy));
  }

  static void Free() {
    PageTableSlotAllocator allocator(2);
    uint32_t index0;
    EXPECT_TRUE(allocator.Alloc(&index0));
    EXPECT_TRUE(allocator.slot_busy_[index0]);
    allocator.Free(index0);
    EXPECT_FALSE(allocator.slot_busy_[index0]);
  }
};

TEST(PageTableSlotAllocator, Alloc) { TestPageTableSlotAllocator::Alloc(); }

TEST(PageTableSlotAllocator, Free) { TestPageTableSlotAllocator::Free(); }
