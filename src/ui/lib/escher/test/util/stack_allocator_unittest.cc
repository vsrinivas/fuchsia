// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/stack_allocator.h"

#include <gtest/gtest.h>

namespace {
using namespace escher;

TEST(StackAllocator, Integers) {
  StackAllocator<int64_t, 1000> alloc;
  int64_t* pair = alloc.Allocate(2);
  pair[0] = 32;
  pair[1] = 24;
  int64_t* the_rest = alloc.Allocate(998);
  int64_t* no_more = alloc.Allocate(1);
  EXPECT_NE(pair, nullptr);
  EXPECT_NE(the_rest, nullptr);
  EXPECT_EQ(no_more, nullptr);

  // Memory is left in its previous state after the allocator is reset.  This
  // isn't guaranteed by the API, but this checks that the implementation isn't
  // slowing things down by unnecessarily overwriting the memory.
  alloc.Reset();
  pair = alloc.Allocate(2);
  EXPECT_EQ(pair[0], 32);
  EXPECT_EQ(pair[1], 24);
  pair[0] = 33;
  pair[1] = 25;
  alloc.Reset();
  pair = alloc.Allocate(2);
  EXPECT_EQ(pair[0], 33);
  EXPECT_EQ(pair[1], 25);

  // Memory can be pre-initialized to a default value via AllocateFilled().
  alloc.Reset();
  pair = alloc.AllocateFilled(2);
  EXPECT_EQ(pair[0], 0);
  EXPECT_EQ(pair[1], 0);
  alloc.Reset();
  pair = alloc.Allocate(2);
  EXPECT_EQ(pair[0], 0);
  EXPECT_EQ(pair[1], 0);

  // Memory can be pre-initialized to a specific value via AllocateFilled().
  alloc.Reset();
  pair = alloc.AllocateFilled(2, 19);
  EXPECT_EQ(pair[0], 19);
  EXPECT_EQ(pair[1], 19);
  alloc.Reset();
  pair = alloc.Allocate(2);
  EXPECT_EQ(pair[0], 19);
  EXPECT_EQ(pair[1], 19);
}

TEST(StackAllocator, ConstructableObjects) {
  // Foo is default-constructible, and also has a 2-arg constructor.
  class Foo {
   public:
    Foo() : Foo(16, 32) {}
    Foo(int a, int b) : a_(a), b_(b) { c_ = a_ + b_; }

    // Uncommenting this results in a compilation error, due to the requirement
    // that StackAllocator can only be used with trivially-destructible types.
    // ~Foo() { a = 0; b = 0; c = 0; }

    int a() const { return a_; }
    int b() const { return b_; }
    int c() const { return c_; }

   private:
    int a_, b_, c_;
  };

  StackAllocator<Foo, 1000> alloc;

  // Allocate half of the allocator's capacity, initializing each Foo with a
  // default value.
  Foo* foo = alloc.AllocateFilled(500);
  for (size_t i = 0; i < 500; ++i) {
    EXPECT_EQ(foo[i].a(), 16);
    EXPECT_EQ(foo[i].b(), 32);
    EXPECT_EQ(foo[i].c(), 48);
  }

  // Allocate the other half of the allocator's capacity, initializing each Foo
  // with a specified value.
  foo = alloc.AllocateFilled(500, Foo(11, 22));
  for (size_t i = 0; i < 500; ++i) {
    EXPECT_EQ(foo[i].a(), 11);
    EXPECT_EQ(foo[i].b(), 22);
    EXPECT_EQ(foo[i].c(), 33);
  }

  // No more space.
  foo = alloc.AllocateFilled(1);
  EXPECT_EQ(foo, nullptr);

  // Memory is left in its previous state after the allocator is reset.  This
  // isn't guaranteed by the API, but this checks that the implementation isn't
  // slowing things down by unnecessarily overwriting the memory.
  alloc.Reset();
  foo = alloc.Allocate(1000);
  for (size_t i = 0; i < 500; ++i) {
    EXPECT_EQ(foo[i].a(), 16);
    EXPECT_EQ(foo[i].b(), 32);
    EXPECT_EQ(foo[i].c(), 48);
  }
  for (size_t i = 500; i < 1000; ++i) {
    EXPECT_EQ(foo[i].a(), 11);
    EXPECT_EQ(foo[i].b(), 22);
    EXPECT_EQ(foo[i].c(), 33);
  }

  // No more space.
  foo = alloc.AllocateFilled(1);
  EXPECT_EQ(foo, nullptr);
}

}  // namespace
