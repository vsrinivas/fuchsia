// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/arena.h"

#include <lib/fdf/cpp/arena.h>

#include <unordered_set>

#include <zxtest/zxtest.h>

TEST(fdf_arena, AllocateMultiple) {
  fdf_arena* arena;
  ASSERT_EQ(ZX_OK, fdf_arena::Create(0, 'AREN', &arena));

  void* addr1 = arena->Allocate(64);
  EXPECT_NOT_NULL(addr1);

  void* addr2 = arena->Allocate(64);
  EXPECT_NOT_NULL(addr2);

  EXPECT_NE(addr1, addr2);

  arena->Destroy();
}

TEST(fdf_arena, AllocateLarge) {
  fdf_arena* arena;
  ASSERT_EQ(ZX_OK, fdf_arena::Create(0, 'AREN', &arena));

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
  ASSERT_EQ(ZX_OK, fdf_arena::Create(0, 'AREN', &arena));

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
  ASSERT_EQ(ZX_OK, fdf_arena::Create(0, 'AREN', &arena));

  EXPECT_FALSE(arena->Contains(reinterpret_cast<void*>(0xFFFFFFFF), 0x10));

  void* addr = arena->Allocate(0x500);
  EXPECT_NOT_NULL(addr);

  EXPECT_TRUE(arena->Contains(addr, 0x500));
  // This range is contained in the initial buffer, but not yet allocated by the user.
  EXPECT_FALSE(arena->Contains(increment_ptr(addr, 0x500), 0x500));

  arena->Destroy();
}

TEST(fdf_arena, FidlArena) {
  fdf::Arena arena('TEST');

  uint8_t* ptr = arena.Allocate(4000, 1, nullptr);
  ASSERT_NOT_NULL(ptr);
  memset(ptr, 1, 4000);
  ASSERT_TRUE(std::all_of(ptr, ptr + 4000, [](uint8_t i) { return i == 1; }));

  uint8_t* ptr2 = arena.Allocate(8000, 1, nullptr);
  ASSERT_NOT_NULL(ptr2);
  memset(ptr2, 2, 8000);
  ASSERT_TRUE(std::all_of(ptr2, ptr2 + 8000, [](uint8_t i) { return i == 2; }));
  ASSERT_NE(ptr, ptr2);

  uint8_t* ptr3 = arena.Allocate(20000, 1, nullptr);
  ASSERT_NOT_NULL(ptr3);
  memset(ptr3, 3, 20000);
  ASSERT_TRUE(std::all_of(ptr3, ptr3 + 20000, [](uint8_t i) { return i == 3; }));
  ASSERT_NE(ptr, ptr3);
  ASSERT_NE(ptr2, ptr3);
}

// Tests that we get unique pointers for allocations from the same |fidl::AnyArena|.
TEST(fdf_arena, FidlArenaAllocateMany) {
  constexpr uint32_t kAllocSize = 1000u;
  constexpr uint32_t kAllocCount = 1;
  constexpr uint32_t kIterations = 1000;

  std::unordered_set<uint8_t*> allocations;

  fdf::Arena arena('TEST');
  for (uint32_t i = 0; i < kIterations; i++) {
    auto ptr = arena.Allocate(kAllocSize, kAllocCount, nullptr);
    ASSERT_NOT_NULL(ptr);
    ASSERT_EQ(allocations.find(ptr), allocations.end());
    allocations.insert(ptr);
  }
}

TEST(fdf_arena, FidlArenaDestructorFunctionCalled) {
  bool destructor_called = false;

  // We will receive a pointer to |destructor_called| in |data|.
  auto destructor = [](uint8_t* data, size_t count) {
    bool** called_ptr = reinterpret_cast<bool**>(data);
    bool* called = *called_ptr;
    *called = true;
  };

  {
    fdf::Arena arena('TEST');
    auto ptr = arena.Allocate(0x1000, 1, destructor);
    bool** called_ptr = reinterpret_cast<bool**>(ptr);
    *called_ptr = &destructor_called;
  }
  ASSERT_TRUE(destructor_called);
}

// Tests that we are freeing allocated |fidl::AnyArena| allocations correctly.
TEST(fdf_arena, FidlArenaAllocationsAreFreed) {
  constexpr uint32_t kAllocSize = 1000 * 1000;
  constexpr uint32_t kAllocCount = 1;
  constexpr uint32_t kIterations = 100000;

  for (uint32_t i = 0; i < kIterations; i++) {
    fdf::Arena arena('TEST');
    auto ptr = arena.Allocate(kAllocSize, kAllocCount, nullptr);
    ASSERT_NOT_NULL(ptr);
  }
}
