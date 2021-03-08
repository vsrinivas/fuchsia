// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/allocator.h>
#include <lib/fidl/llcpp/heap_allocator.h>
#include <lib/fidl/llcpp/vector_view.h>

#include <gtest/gtest.h>

// These tests cover fidl::Allocator template methods which delegate to make<T>() or make<T[]>().
// The make<T>() and make<T[]>() functionality is covered separately by allocator-specific tests.

fidl::HeapAllocator allocator;

TEST(Allocator, MakeVecCount) {
  constexpr size_t kCount = 16 * 1024;
  fidl::VectorView<uint32_t> vec = allocator.make_vec<uint32_t>(kCount);
  EXPECT_EQ(kCount, vec.count());
  for (size_t i = 0; i < kCount; ++i) {
    // None of these should crash.
    vec[i] = 12;
  }
}

TEST(Allocator, MakeVecCountCapacity) {
  constexpr size_t kCount = 4 * 1024;
  constexpr size_t kCapacity = 16 * 1024;
  fidl::VectorView<uint32_t> vec = allocator.make_vec<uint32_t>(kCount, kCapacity);
  EXPECT_EQ(kCount, vec.count());
  vec.set_count(kCapacity);
  EXPECT_EQ(kCapacity, vec.count());
  for (size_t i = 0; i < kCount; ++i) {
    // None of these should crash.
    vec[i] = 12;
  }
}

TEST(Allocator, MakeVecPtrCount) {
  constexpr size_t kCount = 16 * 1024;
  fidl::tracking_ptr<fidl::VectorView<uint32_t>> vec = allocator.make_vec_ptr<uint32_t>(kCount);
  EXPECT_EQ(kCount, vec->count());
  for (size_t i = 0; i < kCount; ++i) {
    // None of these should crash.
    (*vec)[i] = 12;
  }
}

TEST(Allocaotr, MakeVecPtrCountCapacity) {
  constexpr size_t kCount = 4 * 1024;
  constexpr size_t kCapacity = 16 * 1024;
  fidl::tracking_ptr<fidl::VectorView<uint32_t>> vec =
      allocator.make_vec_ptr<uint32_t>(kCount, kCapacity);
  EXPECT_EQ(kCount, vec->count());
  vec->set_count(kCapacity);
  EXPECT_EQ(kCapacity, vec->count());
  for (size_t i = 0; i < kCount; ++i) {
    // None of these should crash.
    (*vec)[i] = 12;
  }
}
