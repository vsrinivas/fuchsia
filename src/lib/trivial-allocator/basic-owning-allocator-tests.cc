// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/trivial-allocator/basic-owning-allocator.h>

#include <cstdint>
#include <memory>
#include <new>

#include <zxtest/zxtest.h>

namespace {

std::unique_ptr<std::byte[]> StdAllocator(size_t& size, size_t alignment) {
  return std::unique_ptr<std::byte[]>(new std::byte[size]);
}

TEST(TrivialAllocatorTests, BasicOwningAllocator) {
  auto allocator = trivial_allocator::BasicOwningAllocator(StdAllocator);

  std::array<int*, 100> held_allocations;
  std::array<int*, 100> aligned_allocations;
  std::array<int*, 100> released_allocations;

  for (int*& iptr : held_allocations) {
    void* ptr = allocator.allocate(sizeof(int), alignof(int));
    EXPECT_NOT_NULL(ptr);
    iptr = reinterpret_cast<int*>(ptr);
    *iptr = 17;
  }

  constexpr size_t kBigAlignment = 128;
  for (int*& iptr : aligned_allocations) {
    void* ptr = allocator.allocate(sizeof(int), kBigAlignment);
    EXPECT_NOT_NULL(ptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) & (kBigAlignment - 1), 0);
    iptr = reinterpret_cast<int*>(ptr);
    *iptr = 42;
  }

  for (int*& iptr : released_allocations) {
    void* ptr = allocator.allocate(sizeof(int), alignof(int));
    EXPECT_NOT_NULL(ptr);
    iptr = reinterpret_cast<int*>(ptr);
    *iptr = 23;
    allocator.deallocate(ptr);
  }

  for (volatile int* vptr : held_allocations) {
    EXPECT_EQ(17, *vptr);
  }

  for (volatile int* vptr : aligned_allocations) {
    EXPECT_EQ(42, *vptr);
  }

  for (volatile int* vptr : released_allocations) {
    EXPECT_EQ(23, *vptr);
  }

  // There should be no leaks found by sanitizers since the allocator owns all.
}

}  // namespace
