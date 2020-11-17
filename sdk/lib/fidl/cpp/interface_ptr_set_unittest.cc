// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/interface_ptr_set.h"

#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

class BoundFrobinatorImpl : public test::FrobinatorImpl {
 public:
  BoundFrobinatorImpl() : binding_(this) {}

  Binding<fidl::test::frobinator::Frobinator>& binding() { return binding_; }

 private:
  Binding<fidl::test::frobinator::Frobinator> binding_;
};

TEST(InterfacePtrSet, Trivial) { InterfacePtrSet<fidl::test::frobinator::Frobinator> ptr_set; }

TEST(InterfacePtrSet, Control) {
  constexpr size_t kCount = 10;

  fidl::test::frobinator::FrobinatorPtr ptrs[kCount];
  BoundFrobinatorImpl impls[kCount];

  InterfacePtrSet<fidl::test::frobinator::Frobinator> ptr_set;

  fidl::test::AsyncLoopForTest loop;

  for (size_t i = 0; i < kCount; ++i)
    impls[i].binding().Bind(ptrs[i].NewRequest());

  EXPECT_EQ(0u, ptr_set.size());
  for (size_t i = 0; i < kCount; ++i)
    ptr_set.AddInterfacePtr(std::move(ptrs[i]));
  EXPECT_EQ(kCount, ptr_set.size());

  for (const auto& impl : impls)
    EXPECT_TRUE(impl.frobs.empty());

  size_t iter_count = 0;
  for (const auto& ptr : ptr_set.ptrs()) {
    ++iter_count;
    (*ptr)->Frob("three");
  }

  EXPECT_EQ(kCount, iter_count);

  loop.RunUntilIdle();

  for (const auto& impl : impls)
    EXPECT_EQ(1u, impl.frobs.size());

  for (size_t i = 0; i < kCount / 2; ++i)
    impls[i].binding().Unbind();

  EXPECT_EQ(kCount, ptr_set.size());
  loop.RunUntilIdle();
  EXPECT_EQ(kCount / 2, ptr_set.size());

  for (const auto& ptr : ptr_set.ptrs())
    (*ptr)->Frob("two");

  loop.RunUntilIdle();

  for (size_t i = 0; i < kCount; ++i) {
    size_t expected = (i < kCount / 2 ? 1 : 2);
    EXPECT_EQ(expected, impls[i].frobs.size());
  }

  ptr_set.CloseAll();

  EXPECT_EQ(0u, ptr_set.size());

  for (const auto& ptr : ptr_set.ptrs())
    (*ptr)->Frob("three");

  loop.RunUntilIdle();

  for (size_t i = 0; i < kCount; ++i) {
    size_t expected = (i < kCount / 2 ? 1 : 2);
    EXPECT_EQ(expected, impls[i].frobs.size());
  }
}

}  // namespace
}  // namespace fidl
