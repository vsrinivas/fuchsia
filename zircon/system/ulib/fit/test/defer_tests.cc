// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <memory>

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/fit/nullable.h>
#include <unittest/unittest.h>

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
bool default_construction() {
  BEGIN_TEST;

  fit::deferred_action<T> d;
  EXPECT_FALSE(d);

  END_TEST;
}

template <typename T>
bool null_construction() {
  BEGIN_TEST;

  fit::deferred_action<T> d(nullptr);
  EXPECT_FALSE(d);

  END_TEST;
}

template <typename T>
bool basic() {
  static_assert(fit::is_nullable<fit::deferred_action<T>>::value, "");

  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool cancel() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool null_assignment() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool target_reassignment() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool call() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool recursive_call() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool move_construct_basic() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool move_construct_from_canceled() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool move_construct_from_called() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool move_assign_basic() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool move_assign_wider_scoped() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool move_assign_from_canceled() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool move_assign_from_called() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool move_assign_to_null() {
  BEGIN_TEST;

  int call_count = 0;
  {
    fit::deferred_action<T> deferred(nullptr);
    EXPECT_FALSE(deferred);
    deferred = fit::defer<T>([&call_count] { call_count++; });
    EXPECT_EQ(0, call_count);
  }
  EXPECT_EQ(1, call_count);

  END_TEST;
}

template <typename T>
bool move_assign_to_invalid() {
  BEGIN_TEST;

  int call_count = 0;
  {
    T fn;
    fit::deferred_action<T> deferred(std::move(fn));
    EXPECT_FALSE(deferred);
    deferred = fit::defer<T>([&call_count] { call_count++; });
    EXPECT_EQ(0, call_count);
  }
  EXPECT_EQ(1, call_count);

  END_TEST;
}

template <typename T>
bool target_destroyed_when_scope_exited() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool target_destroyed_when_called() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool target_destroyed_when_canceled() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool target_destroyed_when_move_constructed() {
  BEGIN_TEST;

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

  END_TEST;
}

template <typename T>
bool target_destroyed_when_move_assigned() {
  BEGIN_TEST;

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

  END_TEST;
}

bool deferred_callback() {
  BEGIN_TEST;

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

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(defer_tests)
RUN_TEST(default_construction<fit::closure>)
RUN_TEST(default_construction<std::function<void()>>)
RUN_TEST(null_construction<fit::closure>)
RUN_TEST(null_construction<std::function<void()>>)
RUN_TEST(basic<fit::closure>)
RUN_TEST(basic<std::function<void()>>)
RUN_TEST(cancel<fit::closure>)
RUN_TEST(cancel<std::function<void()>>)
RUN_TEST(null_assignment<fit::closure>)
RUN_TEST(null_assignment<std::function<void()>>)
RUN_TEST(target_reassignment<fit::closure>)
RUN_TEST(target_reassignment<std::function<void()>>)
RUN_TEST(call<fit::closure>)
RUN_TEST(call<std::function<void()>>)
RUN_TEST(recursive_call<fit::closure>)
RUN_TEST(recursive_call<std::function<void()>>)
RUN_TEST(move_construct_basic<fit::closure>)
RUN_TEST(move_construct_basic<std::function<void()>>)
RUN_TEST(move_construct_from_canceled<fit::closure>)
RUN_TEST(move_construct_from_canceled<std::function<void()>>)
RUN_TEST(move_construct_from_called<fit::closure>)
RUN_TEST(move_construct_from_called<std::function<void()>>)
RUN_TEST(move_assign_basic<fit::closure>)
RUN_TEST(move_assign_basic<std::function<void()>>)
RUN_TEST(move_assign_wider_scoped<fit::closure>)
RUN_TEST(move_assign_wider_scoped<std::function<void()>>)
RUN_TEST(move_assign_from_canceled<fit::closure>)
RUN_TEST(move_assign_from_canceled<std::function<void()>>)
RUN_TEST(move_assign_from_called<fit::closure>)
RUN_TEST(move_assign_from_called<std::function<void()>>)
RUN_TEST(move_assign_to_null<fit::closure>)
RUN_TEST(move_assign_to_null<std::function<void()>>)
RUN_TEST(move_assign_to_invalid<fit::closure>)
RUN_TEST(move_assign_to_invalid<std::function<void()>>)
// These tests do not support std::function because std::function copies
// the captured values (which balance does not support).
RUN_TEST(target_destroyed_when_scope_exited<fit::closure>)
RUN_TEST(target_destroyed_when_called<fit::closure>)
RUN_TEST(target_destroyed_when_canceled<fit::closure>)
RUN_TEST(target_destroyed_when_move_constructed<fit::closure>)
RUN_TEST(target_destroyed_when_move_assigned<fit::closure>)
RUN_TEST(deferred_callback)
END_TEST_CASE(defer_tests)
