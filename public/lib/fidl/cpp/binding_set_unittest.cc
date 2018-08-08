// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/binding_set.h"

#include "gtest/gtest.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

TEST(BindingSet, Trivial) {
  BindingSet<fidl::test::frobinator::Frobinator> binding_set;
}

TEST(BindingSet, Control) {
  constexpr size_t kCount = 10;

  fidl::test::frobinator::FrobinatorPtr ptrs[kCount];
  test::FrobinatorImpl impls[kCount];

  BindingSet<fidl::test::frobinator::Frobinator> binding_set;

  int empty_count = 0;
  binding_set.set_empty_set_handler([&empty_count]() { ++empty_count; });

  fidl::test::AsyncLoopForTest loop;

  for (size_t i = 0; i < kCount; ++i) {
    if (i % 2 == 0) {
      binding_set.AddBinding(&impls[i], ptrs[i].NewRequest());
    } else {
      ptrs[i] = binding_set.AddBinding(&impls[i]).Bind();
    }
  }

  EXPECT_EQ(kCount, binding_set.size());

  for (const auto& impl : impls)
    EXPECT_TRUE(impl.frobs.empty());

  for (auto& ptr : ptrs)
    ptr->Frob("one");

  loop.RunUntilIdle();

  for (const auto& impl : impls)
    EXPECT_EQ(1u, impl.frobs.size());

  for (size_t i = 0; i < kCount / 2; ++i)
    ptrs[i].Unbind();

  loop.RunUntilIdle();

  EXPECT_EQ(kCount / 2, binding_set.size());
  EXPECT_EQ(0, empty_count);

  for (size_t i = kCount / 2; i < kCount; ++i)
    ptrs[i]->Frob("two");

  loop.RunUntilIdle();

  for (size_t i = 0; i < kCount; ++i) {
    size_t expected = (i < kCount / 2 ? 1 : 2);
    EXPECT_EQ(expected, impls[i].frobs.size());
  }

  binding_set.CloseAll();
  EXPECT_EQ(0u, binding_set.size());
  EXPECT_EQ(0, empty_count);

  for (size_t i = kCount / 2; i < kCount; ++i)
    ptrs[i]->Frob("three");

  loop.RunUntilIdle();

  for (size_t i = 0; i < kCount; ++i) {
    size_t expected = (i < kCount / 2 ? 1 : 2);
    EXPECT_EQ(expected, impls[i].frobs.size());
  }
}

TEST(BindingSet, Iterator) {
  constexpr size_t kCount = 2;
  fidl::test::frobinator::FrobinatorPtr ptrs[kCount];
  test::FrobinatorImpl impls[kCount];

  BindingSet<fidl::test::frobinator::Frobinator> binding_set;

  fidl::test::AsyncLoopForTest loop;

  for (size_t i = 0; i < kCount; i++)
    ptrs[i] = binding_set.AddBinding(&impls[i]).Bind();

  EXPECT_EQ(kCount, binding_set.size());

  auto it = binding_set.bindings().begin();
  EXPECT_EQ((*it)->impl(), &impls[0]);
  ++it;
  EXPECT_EQ((*it)->impl(), &impls[1]);
  ++it;
  EXPECT_EQ(it, binding_set.bindings().end());
}

TEST(BindingSet, EmptyHandler) {
  constexpr size_t kCount = 4;

  fidl::test::frobinator::FrobinatorPtr ptrs[kCount];
  test::FrobinatorImpl impls[kCount];

  BindingSet<fidl::test::frobinator::Frobinator> binding_set;

  int empty_count = 0;
  binding_set.set_empty_set_handler([&empty_count]() { ++empty_count; });

  fidl::test::AsyncLoopForTest loop;

  for (size_t i = 0; i < kCount; ++i)
    binding_set.AddBinding(&impls[i], ptrs[i].NewRequest());

  EXPECT_EQ(kCount, binding_set.size());

  EXPECT_EQ(0, empty_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, empty_count);

  for (size_t i = 0; i < kCount - 1; ++i)
    ptrs[i].Unbind();

  EXPECT_EQ(0, empty_count);
  EXPECT_EQ(kCount, binding_set.size());
  loop.RunUntilIdle();
  EXPECT_EQ(0, empty_count);
  EXPECT_EQ(1u, binding_set.size());

  ptrs[kCount - 1].Unbind();

  EXPECT_EQ(0, empty_count);
  EXPECT_EQ(1u, binding_set.size());
  loop.RunUntilIdle();
  EXPECT_EQ(1, empty_count);
  EXPECT_EQ(0u, binding_set.size());
}

TEST(BindingSet, EmptyHandlerOnManualClose) {
  fidl::test::frobinator::FrobinatorPtr ptr;
  test::FrobinatorImpl impl;

  BindingSet<fidl::test::frobinator::Frobinator> binding_set;

  int empty_count = 0;
  binding_set.set_empty_set_handler([&empty_count]() { ++empty_count; });

  fidl::test::AsyncLoopForTest loop;

  // Add the binding.
  binding_set.AddBinding(&impl, ptr.NewRequest());
  EXPECT_EQ(1u, binding_set.size());
  EXPECT_EQ(0, empty_count);

  // Run till idle, nothing should change.
  loop.RunUntilIdle();
  EXPECT_EQ(1u, binding_set.size());
  EXPECT_EQ(0, empty_count);

  // Remove the binding manually by finding it in the set.
  auto found = binding_set.bindings().begin();
  ASSERT_NE(found, binding_set.bindings().end());
  binding_set.CloseAndCheckForEmpty(found);
  EXPECT_EQ(0u, binding_set.size());
  EXPECT_EQ(1, empty_count);

  // Run till idle, nothing should change.
  loop.RunUntilIdle();
  EXPECT_EQ(0u, binding_set.size());
  EXPECT_EQ(1, empty_count);

  // Unbinding should not do anything since it is already not part of the set.
  ptr.Unbind();
  loop.RunUntilIdle();
  EXPECT_EQ(0u, binding_set.size());
  EXPECT_EQ(1, empty_count);
}

}  // namespace
}  // namespace fidl
