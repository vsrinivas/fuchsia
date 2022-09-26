// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/binding_set.h"

#include <memory>

#include <zxtest/zxtest.h>

#include "testing/fidl/async_loop_for_test.h"
#include "testing/fidl/frobinator_impl.h"

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

TEST(BindingSet, BogusCloseBinding) {
  BindingSet<fidl::test::frobinator::Frobinator, test::FrobinatorImpl*> binding_set;
  test::FrobinatorImpl impl;
  constexpr auto kEpitaphValue = ZX_ERR_ADDRESS_UNREACHABLE;
  EXPECT_FALSE(binding_set.CloseBinding(&impl, kEpitaphValue));
  EXPECT_TRUE(binding_set.bindings().empty());
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

TEST(BindingSet, ErrorHandlerFunctionMoveOnly) {
  BindingSet<fidl::test::frobinator::Frobinator> binding_set;
  fidl::test::AsyncLoopForTest loop;

  // Use the reference counting of a `shared_ptr` canary to make sure that the lambda capture
  // context is never copied.
  auto ref_counted_canary = std::make_shared<int>(1337);
  std::weak_ptr<int> ref_counted_canary_weak = ref_counted_canary;
  ASSERT_EQ(ref_counted_canary_weak.use_count(), 1);

  std::unique_ptr<int> force_move_only;

  test::FrobinatorImpl impl;
  fidl::test::frobinator::FrobinatorPtr ptr;

  binding_set.AddBinding(&impl, ptr.NewRequest(), nullptr,
                         [ref_counted_canary = std::move(ref_counted_canary),
                          force_move_only = std::move(force_move_only)](zx_status_t) {});

  // There still should only be one reference to the canary (the lambda capture).
  ASSERT_EQ(ref_counted_canary_weak.use_count(), 1);

  // Trigger error.
  ptr.Unbind();
  loop.RunUntilIdle();

  // The canary should've been dropped and cleaned up.
  ASSERT_TRUE(ref_counted_canary_weak.expired());
}

TEST(BindingSet, ErrorHandlerFunctionValidWhenInvoked) {
  // Declared outside of `MoveOnlyCanary` because local classes may not have static members.
  constexpr uint64_t kAliveCanaryValue = 0x1234567890ABCDEF;
  constexpr uint64_t kMovedFromCanaryValue = 0xFEDCBA0987654321;
  constexpr uint64_t kDeadCanaryValue = 0;
  static_assert(kAliveCanaryValue != kMovedFromCanaryValue &&
                    kAliveCanaryValue != kDeadCanaryValue &&
                    kMovedFromCanaryValue != kDeadCanaryValue,
                "Different canary values cannot be equivalent");

  // A move-only type with a destructor that uses a canary to detect whether it has previously been
  // destructed.
  class MoveOnlyCanary {
   public:
    constexpr MoveOnlyCanary() = default;

    MoveOnlyCanary(MoveOnlyCanary&& other) : canary_(other.TakeCanary()) {}

    MoveOnlyCanary& operator=(MoveOnlyCanary&& other) {
      if (this == &other) {
        return *this;
      }

      canary_ = other.TakeCanary();
      return *this;
    }

    MoveOnlyCanary(const MoveOnlyCanary&) = delete;
    MoveOnlyCanary& operator=(const MoveOnlyCanary&) = delete;

    ~MoveOnlyCanary() { Kill(); }

    bool IsAlive() const { return canary_ == kAliveCanaryValue; }

    bool IsMovedFrom() const { return canary_ == kMovedFromCanaryValue; }

   private:
    void Kill() {
      ASSERT_TRUE(IsMovedFrom() || IsAlive());
      canary_ = kDeadCanaryValue;
    }

    uint64_t TakeCanary() {
      EXPECT_TRUE(IsAlive());

      uint64_t ret = canary_;
      canary_ = kMovedFromCanaryValue;
      return ret;
    }

    uint64_t canary_ = kAliveCanaryValue;
  };

  BindingSet<fidl::test::frobinator::Frobinator> binding_set;
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  fidl::test::frobinator::FrobinatorPtr ptr;

  MoveOnlyCanary canary;
  ASSERT_TRUE(canary.IsAlive());

  bool handler_called = false;

  binding_set.AddBinding(
      &impl, ptr.NewRequest(), nullptr,
      [canary = std::move(canary), &handler_called](zx_status_t) { handler_called = true; });

  // Trigger error.
  ptr.Unbind();
  loop.RunUntilIdle();

  ASSERT_TRUE(handler_called);
}

}  // namespace
}  // namespace fidl
