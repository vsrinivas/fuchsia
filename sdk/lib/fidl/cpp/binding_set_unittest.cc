// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/binding_set.h"

#include "gtest/gtest.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

TEST(BindingSet, Trivial) { BindingSet<fidl::test::frobinator::Frobinator> binding_set; }

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

  // Unbind and wait until the binding has been removed from the binding set.
  ptr.Unbind();
  loop.RunUntilIdle();
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

TEST(BindingSet, BindingDestroyedAfterRemovalFromSet) {
  fidl::test::AsyncLoopForTest loop;

  BindingSet<fidl::test::frobinator::Frobinator, std::unique_ptr<test::FrobinatorImpl>> binding_set;
  auto check_binding_set_empty_on_destroy = [&binding_set] {
    EXPECT_TRUE(binding_set.bindings().empty());
  };

  fidl::test::frobinator::FrobinatorPtr ptr;

  // Add the binding.
  binding_set.AddBinding(std::make_unique<test::FrobinatorImpl>(check_binding_set_empty_on_destroy),
                         ptr.NewRequest());
  EXPECT_EQ(1u, binding_set.size());

  // Unbind and wait until the binding has been removed from the binding set.
  ptr.Unbind();
  loop.RunUntilIdle();
  EXPECT_TRUE(binding_set.bindings().empty());
}

TEST(BindingSet, BindingDestroyedAfterCloseAll) {
  fidl::test::AsyncLoopForTest loop;

  BindingSet<fidl::test::frobinator::Frobinator, std::unique_ptr<test::FrobinatorImpl>> binding_set;
  auto check_binding_set_empty_on_destroy = [&binding_set] {
    EXPECT_TRUE(binding_set.bindings().empty());
  };

  fidl::test::frobinator::FrobinatorPtr ptr;

  // Add the binding.
  binding_set.AddBinding(std::make_unique<test::FrobinatorImpl>(check_binding_set_empty_on_destroy),
                         ptr.NewRequest());
  EXPECT_EQ(1u, binding_set.size());

  binding_set.CloseAll();
  loop.RunUntilIdle();
  EXPECT_TRUE(binding_set.bindings().empty());
}

TEST(BindingSet, EpitaphSentWithCloseAll) {
  fidl::test::AsyncLoopForTest loop;

  BindingSet<fidl::test::frobinator::Frobinator, std::unique_ptr<test::FrobinatorImpl>> binding_set;

  // Attach an error handler.
  fidl::test::frobinator::FrobinatorPtr ptr;
  bool client_error_handler_invoked = false;
  zx_status_t client_error_handler_status = ZX_OK;
  ptr.set_error_handler([&](zx_status_t status) {
    client_error_handler_status = status;
    client_error_handler_invoked = true;
  });

  // Add the binding.
  binding_set.AddBinding(std::make_unique<test::FrobinatorImpl>(), ptr.NewRequest());
  EXPECT_EQ(1u, binding_set.size());

  constexpr auto kEpitaphValue = ZX_ERR_ADDRESS_UNREACHABLE;
  binding_set.CloseAll(kEpitaphValue);
  loop.RunUntilIdle();
  EXPECT_TRUE(binding_set.bindings().empty());
  ASSERT_TRUE(client_error_handler_invoked);
  EXPECT_EQ(client_error_handler_status, kEpitaphValue);
}

TEST(BindingSet, EpitaphSentWithClose) {
  fidl::test::AsyncLoopForTest loop;

  BindingSet<fidl::test::frobinator::Frobinator, test::FrobinatorImpl*> binding_set;

  // Attach an error handler.
  fidl::test::frobinator::FrobinatorPtr ptr;
  bool client_error_handler_invoked = false;
  zx_status_t client_error_handler_status = ZX_OK;
  ptr.set_error_handler([&](zx_status_t status) {
    client_error_handler_status = status;
    client_error_handler_invoked = true;
  });
  test::FrobinatorImpl impl;

  // Add the binding.
  binding_set.AddBinding(&impl, ptr.NewRequest());
  EXPECT_EQ(1u, binding_set.size());

  constexpr auto kEpitaphValue = ZX_ERR_ADDRESS_UNREACHABLE;
  binding_set.CloseBinding(&impl, kEpitaphValue);
  loop.RunUntilIdle();

  EXPECT_TRUE(binding_set.bindings().empty());
  ASSERT_TRUE(client_error_handler_invoked);
  EXPECT_EQ(client_error_handler_status, kEpitaphValue);
}

TEST(BindingSet, CloseBindingsHandlesEmptySet) {
  fidl::test::AsyncLoopForTest loop;

  BindingSet<fidl::test::frobinator::Frobinator, test::FrobinatorImpl*> binding_set;
  bool empty_set_handled = false;
  binding_set.set_empty_set_handler([&] { empty_set_handled = true; });

  fidl::test::frobinator::FrobinatorPtr ptr;
  test::FrobinatorImpl impl;
  fidl::test::frobinator::FrobinatorPtr other_ptr;
  test::FrobinatorImpl other_impl;

  // Add the bindings.
  binding_set.AddBinding(&impl, ptr.NewRequest());
  binding_set.AddBinding(&other_impl, other_ptr.NewRequest());
  loop.RunUntilIdle();

  EXPECT_EQ(2u, binding_set.size());
  EXPECT_FALSE(empty_set_handled);

  // Check that the empty_set_handler is not called when the first binding is removed.
  constexpr auto kEpitaphValue = ZX_ERR_ADDRESS_UNREACHABLE;
  binding_set.CloseBinding(&impl, kEpitaphValue);
  loop.RunUntilIdle();
  EXPECT_FALSE(empty_set_handled);

  // Check that the empty_set_handler is called when the last binding is removed.
  binding_set.CloseBinding(&other_impl, kEpitaphValue);
  loop.RunUntilIdle();
  EXPECT_TRUE(empty_set_handled);
}

TEST(BindingSet, RemoveBindingDeletesTheBinding) {
  fidl::test::AsyncLoopForTest loop;

  BindingSet<fidl::test::frobinator::Frobinator, test::FrobinatorImpl*> binding_set;
  auto check_binding_set_empty_on_destroy = [&binding_set] {
    EXPECT_TRUE(binding_set.bindings().empty());
  };

  test::FrobinatorImpl frobinator(check_binding_set_empty_on_destroy);
  fidl::test::frobinator::FrobinatorPtr ptr;

  // Add the binding.
  binding_set.AddBinding(&frobinator, ptr.NewRequest());
  EXPECT_EQ(1u, binding_set.size());

  // Remove the binding.
  EXPECT_TRUE(binding_set.RemoveBinding(&frobinator));
  EXPECT_TRUE(binding_set.bindings().empty());

  // Try to remove the binding again.
  EXPECT_FALSE(binding_set.RemoveBinding(&frobinator));
}

TEST(BindingSet, ErrorHandlerCalledAfterError) {
  fidl::test::AsyncLoopForTest loop;

  BindingSet<fidl::test::frobinator::Frobinator, std::unique_ptr<test::FrobinatorImpl>> binding_set;
  auto check_binding_set_empty_on_destroy = [&binding_set] {
    EXPECT_TRUE(binding_set.bindings().empty());
  };

  fidl::test::frobinator::FrobinatorPtr ptr;
  bool handler_called = false;

  // Add the binding.
  binding_set.AddBinding(std::make_unique<test::FrobinatorImpl>(check_binding_set_empty_on_destroy),
                         ptr.NewRequest(), nullptr,
                         [&handler_called](zx_status_t) { handler_called = true; });
  EXPECT_FALSE(handler_called);

  // Trigger error.
  ptr.Unbind();
  loop.RunUntilIdle();

  EXPECT_TRUE(handler_called);
}

TEST(BindingSet, ErrorHandlerDestroysBindingSetAndBindings) {
  fidl::test::AsyncLoopForTest loop;

  BindingSet<fidl::test::frobinator::Frobinator, test::FrobinatorImpl*> binding_set;
  auto check_binding_set_empty_on_destroy = [&binding_set] {
    EXPECT_TRUE(binding_set.bindings().empty());
  };

  test::FrobinatorImpl frobinator(check_binding_set_empty_on_destroy);
  fidl::test::frobinator::FrobinatorPtr ptr;
  bool handler_called = false;

  // Add the binding.
  binding_set.AddBinding(&frobinator, ptr.NewRequest(), nullptr,
                         [&handler_called, &binding_set, &frobinator](zx_status_t) {
                           binding_set.RemoveBinding(&frobinator);
                           binding_set.CloseAll();
                           handler_called = true;
                         });
  EXPECT_FALSE(handler_called);

  // Trigger error.
  ptr.Unbind();
  loop.RunUntilIdle();

  EXPECT_TRUE(handler_called);
}

TEST(BindingSet, ErrorHandlerDestroysBindingSetAndBindingsWithUniquePtr) {
  fidl::test::AsyncLoopForTest loop;

  BindingSet<fidl::test::frobinator::Frobinator, std::unique_ptr<test::FrobinatorImpl>> binding_set;
  auto check_binding_set_empty_on_destroy = [&binding_set] {
    EXPECT_TRUE(binding_set.bindings().empty());
  };

  auto frobinator = std::make_unique<test::FrobinatorImpl>(check_binding_set_empty_on_destroy);
  test::FrobinatorImpl* frobinator_raw_ptr = frobinator.get();
  fidl::test::frobinator::FrobinatorPtr ptr;
  bool handler_called = false;

  // Add the binding.
  binding_set.AddBinding(std::move(frobinator), ptr.NewRequest(), nullptr,
                         [&handler_called, &binding_set, frobinator_raw_ptr](zx_status_t) {
                           binding_set.RemoveBinding(frobinator_raw_ptr);
                           binding_set.CloseAll();
                           handler_called = true;
                         });
  EXPECT_FALSE(handler_called);

  // Trigger error.
  ptr.Unbind();
  loop.RunUntilIdle();

  EXPECT_TRUE(handler_called);
}

}  // namespace
}  // namespace fidl
