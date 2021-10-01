// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/arena.h"

#include <zxtest/zxtest.h>

TEST(fdf_arena, AllocateMultiple) {
  fdf_arena* arena;
  ASSERT_EQ(ZX_OK, fdf_arena::Create(0, "arena", &arena));

  void* addr1 = arena->Allocate(64);
  EXPECT_NOT_NULL(addr1);

  void* addr2 = arena->Allocate(64);
  EXPECT_NOT_NULL(addr2);

  EXPECT_NE(addr1, addr2);

  arena->Destroy();
}

TEST(fdf_arena, AllocateLarge) {
  fdf_arena* arena;
  ASSERT_EQ(ZX_OK, fdf_arena::Create(0, "arena", &arena));

  void* addr1 = arena->Allocate(0x100000);
  EXPECT_NOT_NULL(addr1);

  void* addr2 = arena->Allocate(0x600000);
  EXPECT_NOT_NULL(addr2);

  EXPECT_NE(addr1, addr2);

  arena->Destroy();
}

static void* increment_ptr(void* ptr, size_t offset) {
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) + offset);
}

TEST(fdf_arena, Contains) {
  fdf_arena* arena;
  ASSERT_EQ(ZX_OK, fdf_arena::Create(0, "arena", &arena));

  void* addr1 = arena->Allocate(0x1000);
  EXPECT_NOT_NULL(addr1);

  void* addr2 = arena->Allocate(0x10000);
  EXPECT_NOT_NULL(addr2);

  void* addr3 = arena->Allocate(0x500);
  EXPECT_NOT_NULL(addr3);

  EXPECT_FALSE(arena->Contains(0, 0x1));

  EXPECT_TRUE(arena->Contains(addr1, 0x800));
  EXPECT_TRUE(arena->Contains(addr1, 0x1000));
  EXPECT_FALSE(arena->Contains(addr1, 0x1001));

  EXPECT_TRUE(arena->Contains(increment_ptr(addr1, 0x1), 0x999));
  EXPECT_TRUE(arena->Contains(increment_ptr(addr1, 0x800), 0x800));
  // 1 byte past the end of the addr1 allocation
  EXPECT_FALSE(arena->Contains(increment_ptr(addr1, 0x800), 0x801));
  EXPECT_FALSE(arena->Contains(increment_ptr(addr1, 0xfff), 0x2));

  EXPECT_TRUE(arena->Contains(addr2, 0x10000));
  EXPECT_TRUE(arena->Contains(increment_ptr(addr2, 0x9990), 0xf));

  EXPECT_TRUE(arena->Contains(addr3, 0x400));
  EXPECT_TRUE(arena->Contains(addr3, 0x500));
  EXPECT_FALSE(arena->Contains(addr3, 0x501));

  EXPECT_TRUE(arena->Contains(increment_ptr(addr3, 0x5), 0x100));
  EXPECT_TRUE(arena->Contains(increment_ptr(addr3, 0x250), 0x250));
  // 1 byte past the end of the addr3 allocation
  EXPECT_FALSE(arena->Contains(increment_ptr(addr3, 0x500), 0x1));

  arena->Destroy();
}

TEST(fdf_arena, InitialBufferContains) {
  fdf_arena* arena;
  ASSERT_EQ(ZX_OK, fdf_arena::Create(0, "arena", &arena));

  EXPECT_FALSE(arena->Contains(reinterpret_cast<void*>(0xFFFFFFFF), 0x10));

  void* addr = arena->Allocate(0x500);
  EXPECT_NOT_NULL(addr);

  EXPECT_TRUE(arena->Contains(addr, 0x500));
  // This range is contained in the initial buffer, but not yet allocated by the user.
  EXPECT_FALSE(arena->Contains(increment_ptr(addr, 0x500), 0x500));

  arena->Destroy();
}
