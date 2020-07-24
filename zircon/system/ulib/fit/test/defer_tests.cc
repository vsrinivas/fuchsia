// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/fit/nullable.h>

#include <functional>
#include <memory>

#include <zxtest/zxtest.h>

#include "unittest_utils.h"

namespace {

// Counts instances.
class balance {
 public:
  balance(int* counter) : counter_(counter) { *counter_ += 1; }

  balance(balance&& other) : counter_(other.counter_) { *counter_ += 1; }

  ~balance() { *counter_ -= 1; }

  balance(const balance& other) = delete;
  balance& operator=(const balance& other) = delete;
  balance& operator=(balance&& other) = delete;

 private:
  int* const counter_;
};

void incr_arg(int* p) { *p += 1; }

template <typename T>
void default_construction() {
  fit::deferred_action<T> d;
  EXPECT_FALSE(d);
}

template <typename T>
void null_construction() {
  fit::deferred_action<T> d(nullptr);
  EXPECT_FALSE(d);
}

template <typename T>
void basic() {
  static_assert(fit::is_nullable<fit::deferred_action<T>>::value, "");

  int var = 0;
  {
    auto do_incr = fit::defer<T>([&var]() { incr_arg(&var); });
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var, 0);
    EXPECT_FALSE(do_incr == nullptr);
    EXPECT_FALSE(nullptr == do_incr);
    EXPECT_TRUE(do_incr != nullptr);
    EXPECT_TRUE(nullptr != do_incr);
  }
  EXPECT_EQ(var, 1);
}

template <typename T>
void cancel() {
  int var = 0;
  {
    auto do_incr = fit::defer<T>([&var]() { incr_arg(&var); });
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var, 0);

    do_incr.cancel();
    EXPECT_FALSE(do_incr);
    EXPECT_EQ(var, 0);
    EXPECT_TRUE(do_incr == nullptr);
    EXPECT_TRUE(nullptr == do_incr);
    EXPECT_FALSE(do_incr != nullptr);
    EXPECT_FALSE(nullptr != do_incr);

    // Once cancelled, call has no effect.
    do_incr.call();
    EXPECT_FALSE(do_incr);
    EXPECT_EQ(var, 0);
  }
  EXPECT_EQ(var, 0);
}

template <typename T>
void null_assignment() {
  int var = 0;
  {
    auto do_incr = fit::defer<T>([&var]() { incr_arg(&var); });
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var, 0);

    do_incr = nullptr;
    EXPECT_FALSE(do_incr);
    EXPECT_EQ(var, 0);

    // Once cancelled, call has no effect.
    do_incr.call();
    EXPECT_FALSE(do_incr);
    EXPECT_EQ(var, 0);
  }
  EXPECT_EQ(var, 0);
}

template <typename T>
void target_reassignment() {
  int var = 0;
  {
    fit::deferred_action<T> do_incr;
    do_incr = []() { ASSERT_CRITICAL(false); };
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var, 0);

    do_incr = [&var]() { incr_arg(&var); };
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var, 0);
  }
  EXPECT_EQ(var, 1);
}

template <typename T>
void call() {
  int var = 0;
  {
    auto do_incr = fit::defer<T>([&var]() { incr_arg(&var); });
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var, 0);

    do_incr.call();
    EXPECT_FALSE(do_incr);
    EXPECT_EQ(var, 1);

    // Call is effective only once.
    do_incr.call();
    EXPECT_FALSE(do_incr);
    EXPECT_EQ(var, 1);
  }
  EXPECT_EQ(var, 1);
}

template <typename T>
void recursive_call() {
  int var = 0;
  {
    auto do_incr = fit::defer<T>([]() { /* no-op */ });
    EXPECT_TRUE(do_incr);
    do_incr = fit::defer<T>([&do_incr, &var]() {
      incr_arg(&var);
      do_incr.call();
      EXPECT_FALSE(do_incr);
    });
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var, 0);

    do_incr.call();
    EXPECT_FALSE(do_incr);
    EXPECT_EQ(var, 1);
  }
  EXPECT_EQ(var, 1);
}

template <typename T>
void move_construct_basic() {
  int var = 0;
  {
    auto do_incr = fit::defer<T>([&var]() { incr_arg(&var); });
    EXPECT_TRUE(do_incr);

    auto do_incr2(std::move(do_incr));
    EXPECT_FALSE(do_incr);
    EXPECT_TRUE(do_incr2);
    EXPECT_EQ(var, 0);
  }
  EXPECT_EQ(var, 1);
}

template <typename T>
void move_construct_from_canceled() {
  int var = 0;
  {
    auto do_incr = fit::defer<T>([&var]() { incr_arg(&var); });
    EXPECT_TRUE(do_incr);

    do_incr.cancel();
    EXPECT_FALSE(do_incr);

    auto do_incr2(std::move(do_incr));
    EXPECT_FALSE(do_incr);
    EXPECT_FALSE(do_incr2);
    EXPECT_EQ(var, 0);
  }
  EXPECT_EQ(var, 0);
}

template <typename T>
void move_construct_from_called() {
  int var = 0;
  {
    auto do_incr = fit::defer<T>([&var]() { incr_arg(&var); });
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var, 0);

    do_incr.call();
    EXPECT_FALSE(do_incr);
    EXPECT_EQ(var, 1);

    // Must not be called again, since do_incr has triggered already.
    auto do_incr2(std::move(do_incr));
    EXPECT_FALSE(do_incr);
  }
  EXPECT_EQ(var, 1);
}

template <typename T>
void move_assign_basic() {
  int var1 = 0, var2 = 0;
  {
    auto do_incr = fit::defer<T>([&var1]() { incr_arg(&var1); });
    auto do_incr2 = fit::defer<T>([&var2]() { incr_arg(&var2); });
    EXPECT_TRUE(do_incr);
    EXPECT_TRUE(do_incr2);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);

    // do_incr2 is moved-to, so its associated function is called.
    do_incr2 = std::move(do_incr);
    EXPECT_FALSE(do_incr);
    EXPECT_TRUE(do_incr2);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 1);

    // self-assignment does nothing
    do_incr = std::move(do_incr);
    do_incr2 = std::move(do_incr2);
    EXPECT_TRUE(do_incr2);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 1);
  }
  EXPECT_EQ(var1, 1);
  EXPECT_EQ(var2, 1);
}

template <typename T>
void move_assign_wider_scoped() {
  int var1 = 0, var2 = 0;
  {
    auto do_incr = fit::defer<T>([&var1]() { incr_arg(&var1); });
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);
    {
      auto do_incr2 = fit::defer<T>([&var2]() { incr_arg(&var2); });
      EXPECT_TRUE(do_incr);
      EXPECT_TRUE(do_incr2);
      EXPECT_EQ(var1, 0);
      EXPECT_EQ(var2, 0);

      // do_incr is moved-to, so its associated function is called.
      do_incr = std::move(do_incr2);
      EXPECT_TRUE(do_incr);
      EXPECT_FALSE(do_incr2);
      EXPECT_EQ(var1, 1);
      EXPECT_EQ(var2, 0);
    }
    // do_incr2 is out of scope but has been moved so its function is not
    // called.
    EXPECT_TRUE(do_incr);
    EXPECT_EQ(var1, 1);
    EXPECT_EQ(var2, 0);
  }
  EXPECT_EQ(var1, 1);
  EXPECT_EQ(var2, 1);
}

template <typename T>
void move_assign_from_canceled() {
  int var1 = 0, var2 = 0;
  {
    auto do_incr = fit::defer<T>([&var1]() { incr_arg(&var1); });
    auto do_incr2 = fit::defer<T>([&var2]() { incr_arg(&var2); });
    EXPECT_TRUE(do_incr);
    EXPECT_TRUE(do_incr2);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);

    do_incr.cancel();
    EXPECT_FALSE(do_incr);
    EXPECT_TRUE(do_incr2);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);

    // do_incr2 is moved-to, so its associated function is called.
    do_incr2 = std::move(do_incr);
    EXPECT_FALSE(do_incr);
    EXPECT_FALSE(do_incr2);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 1);
  }
  // do_incr was cancelled, this state is preserved by the move.
  EXPECT_EQ(var1, 0);
  EXPECT_EQ(var2, 1);
}

template <typename T>
void move_assign_from_called() {
  int var1 = 0, var2 = 0;
  {
    auto do_incr = fit::defer<T>([&var1]() { incr_arg(&var1); });
    auto do_incr2 = fit::defer<T>([&var2]() { incr_arg(&var2); });
    EXPECT_TRUE(do_incr);
    EXPECT_TRUE(do_incr2);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);

    do_incr.call();
    EXPECT_FALSE(do_incr);
    EXPECT_TRUE(do_incr2);
    EXPECT_EQ(var1, 1);
    EXPECT_EQ(var2, 0);

    // do_incr2 is moved-to, so its associated function is called.
    do_incr2 = std::move(do_incr);
    EXPECT_FALSE(do_incr);
    EXPECT_FALSE(do_incr2);
    EXPECT_EQ(var1, 1);
    EXPECT_EQ(var2, 1);
  }
  // do_incr was called already, this state is preserved by the move.
  EXPECT_EQ(var1, 1);
  EXPECT_EQ(var2, 1);
}

template <typename T>
void move_assign_to_null() {
  int call_count = 0;
  {
    fit::deferred_action<T> deferred(nullptr);
    EXPECT_FALSE(deferred);
    deferred = fit::defer<T>([&call_count] { call_count++; });
    EXPECT_EQ(0, call_count);
  }
  EXPECT_EQ(1, call_count);
}

template <typename T>
void move_assign_to_invalid() {
  int call_count = 0;
  {
    T fn;
    fit::deferred_action<T> deferred(std::move(fn));
    EXPECT_FALSE(deferred);
    deferred = fit::defer<T>([&call_count] { call_count++; });
    EXPECT_EQ(0, call_count);
  }
  EXPECT_EQ(1, call_count);
}

template <typename T>
void target_destroyed_when_scope_exited() {
  int call_count = 0;
  int instance_count = 0;
  {
    auto action =
        fit::defer<T>([&call_count, balance = balance(&instance_count)] { incr_arg(&call_count); });
    EXPECT_EQ(0, call_count);
    EXPECT_EQ(1, instance_count);
  }
  EXPECT_EQ(1, call_count);
  EXPECT_EQ(0, instance_count);
}

template <typename T>
void target_destroyed_when_called() {
  int call_count = 0;
  int instance_count = 0;
  {
    auto action =
        fit::defer<T>([&call_count, balance = balance(&instance_count)] { incr_arg(&call_count); });
    EXPECT_EQ(0, call_count);
    EXPECT_EQ(1, instance_count);

    action.call();
    EXPECT_EQ(1, call_count);
    EXPECT_EQ(0, instance_count);
  }
  EXPECT_EQ(1, call_count);
  EXPECT_EQ(0, instance_count);
}

template <typename T>
void target_destroyed_when_canceled() {
  int call_count = 0;
  int instance_count = 0;
  {
    auto action =
        fit::defer<T>([&call_count, balance = balance(&instance_count)] { incr_arg(&call_count); });
    EXPECT_EQ(0, call_count);
    EXPECT_EQ(1, instance_count);

    action.cancel();
    EXPECT_EQ(0, call_count);
    EXPECT_EQ(0, instance_count);
  }
  EXPECT_EQ(0, call_count);
  EXPECT_EQ(0, instance_count);
}

template <typename T>
void target_destroyed_when_move_constructed() {
  int call_count = 0;
  int instance_count = 0;
  {
    auto action =
        fit::defer<T>([&call_count, balance = balance(&instance_count)] { incr_arg(&call_count); });
    EXPECT_EQ(0, call_count);
    EXPECT_EQ(1, instance_count);

    auto action2(std::move(action));
    EXPECT_EQ(0, call_count);
    EXPECT_EQ(1, instance_count);
  }
  EXPECT_EQ(1, call_count);
  EXPECT_EQ(0, instance_count);
}

template <typename T>
void target_destroyed_when_move_assigned() {
  int call_count = 0;
  int instance_count = 0;
  {
    auto action =
        fit::defer<T>([&call_count, balance = balance(&instance_count)] { incr_arg(&call_count); });
    EXPECT_EQ(0, call_count);
    EXPECT_EQ(1, instance_count);

    auto action2 = fit::defer<T>([] {});
    action2 = std::move(action);
    EXPECT_EQ(0, call_count);
    EXPECT_EQ(1, instance_count);
  }
  EXPECT_EQ(1, call_count);
  EXPECT_EQ(0, instance_count);
}

TEST(DeferTests, deferred_callback) {
  auto get_lambda = [](bool* b) { return [b] { *b = true; }; };

  bool called1 = false;
  bool called2 = false;

  {
    auto deferred_action = fit::defer(get_lambda(&called1));
    fit::deferred_callback deferred_callback = fit::defer_callback(get_lambda(&called2));

    bool is_same_type = std::is_same<decltype(deferred_action), decltype(deferred_callback)>::value;
    EXPECT_FALSE(is_same_type);

    EXPECT_FALSE(called2);
  }
  EXPECT_TRUE(called2);
}

}  // namespace

TEST(DeferTests, default_construction_fit_closure) { default_construction<fit::closure>(); }
TEST(DeferTests, default_construction_std_function) {
  default_construction<std::function<void()>>();
}
TEST(DeferTests, null_construction_fit_closure) { null_construction<fit::closure>(); }
TEST(DeferTests, null_construction_std_function) { null_construction<std::function<void()>>(); }
TEST(DeferTests, basic_fit_closure) { basic<fit::closure>(); }
TEST(DeferTests, basic_std_function) { basic<std::function<void()>>(); }
TEST(DeferTests, cancel_fit_closure) { cancel<fit::closure>(); }
TEST(DeferTests, cancel_std_function) { cancel<std::function<void()>>(); }
TEST(DeferTests, null_assignment_fit_closure) { null_assignment<fit::closure>(); }
TEST(DeferTests, null_assignment_std_function) { null_assignment<std::function<void()>>(); }
TEST(DeferTests, target_reassignment_fit_closure) { target_reassignment<fit::closure>(); }
TEST(DeferTests, target_reassignment_std_function) { target_reassignment<std::function<void()>>(); }
TEST(DeferTests, call_fit_closure) { call<fit::closure>(); }
TEST(DeferTests, call_std_function) { call<std::function<void()>>(); }
TEST(DeferTests, recursive_call_fit_closure) { recursive_call<fit::closure>(); }
TEST(DeferTests, recursive_call_std_function) { recursive_call<std::function<void()>>(); }
TEST(DeferTests, move_construct_basic_fit_closure) { move_construct_basic<fit::closure>(); }
TEST(DeferTests, move_construct_basic_std_function) {
  move_construct_basic<std::function<void()>>();
}
TEST(DeferTests, move_construct_from_canceled_fit_closure) {
  move_construct_from_canceled<fit::closure>();
}
TEST(DeferTests, move_construct_from_canceled_std_function) {
  move_construct_from_canceled<std::function<void()>>();
}
TEST(DeferTests, move_construct_from_called_fit_closure) {
  move_construct_from_called<fit::closure>();
}
TEST(DeferTests, move_construct_from_called_std_function) {
  move_construct_from_called<std::function<void()>>();
}
TEST(DeferTests, move_assign_basic_fit_closure) { move_assign_basic<fit::closure>(); }
TEST(DeferTests, move_assign_basic_std_function) { move_assign_basic<std::function<void()>>(); }
TEST(DeferTests, move_assign_wider_scoped_fit_closure) { move_assign_wider_scoped<fit::closure>(); }
TEST(DeferTests, move_assign_wider_scoped_std_function) {
  move_assign_wider_scoped<std::function<void()>>();
}
TEST(DeferTests, move_assign_from_canceled_fit_closure) {
  move_assign_from_canceled<fit::closure>();
}
TEST(DeferTests, move_assign_from_canceled_std_function) {
  move_assign_from_canceled<std::function<void()>>();
}
TEST(DeferTests, move_assign_from_called_fit_closure) { move_assign_from_called<fit::closure>(); }
TEST(DeferTests, move_assign_from_called_std_function) {
  move_assign_from_called<std::function<void()>>();
}
TEST(DeferTests, move_assign_to_null_fit_closure) { move_assign_to_null<fit::closure>(); }
TEST(DeferTests, move_assign_to_null_std_function) { move_assign_to_null<std::function<void()>>(); }
TEST(DeferTests, move_assign_to_invalid_fit_closure) { move_assign_to_invalid<fit::closure>(); }
TEST(DeferTests, move_assign_to_invalid_std_function) {
  move_assign_to_invalid<std::function<void()>>();
}
// These tests do not support std::function because std::function copies
// the captured values (which balance does not support).
TEST(DeferTests, target_destroyed_when_scope_exited_fit_closure) {
  target_destroyed_when_scope_exited<fit::closure>();
}
TEST(DeferTests, target_destroyed_when_called_fit_closure) {
  target_destroyed_when_called<fit::closure>();
}
TEST(DeferTests, target_destroyed_when_canceled_fit_closure) {
  target_destroyed_when_canceled<fit::closure>();
}
TEST(DeferTests, target_destroyed_when_move_constructed_fit_closure) {
  target_destroyed_when_move_constructed<fit::closure>();
}
TEST(DeferTests, target_destroyed_when_move_assigned_fit_closure) {
  target_destroyed_when_move_assigned<fit::closure>();
}
