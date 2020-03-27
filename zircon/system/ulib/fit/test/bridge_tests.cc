// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <future>
#include <string>
#include <thread>
#include <tuple>

#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fit/single_threaded_executor.h>
#include <unittest/unittest.h>

#include "unittest_utils.h"

namespace {

void async_invoke_callback_no_args(uint64_t* run_count, fit::function<void()> callback) {
  std::thread([run_count, callback = std::move(callback)]() mutable {
    (*run_count)++;
    callback();
  }).detach();
}

void async_invoke_callback_one_arg(uint64_t* run_count, fit::function<void(std::string)> callback) {
  std::thread([run_count, callback = std::move(callback)]() mutable {
    (*run_count)++;
    callback("Hippopotamus");
  }).detach();
}

void async_invoke_callback_two_args(uint64_t* run_count,
                                    fit::function<void(std::string, int)> callback) {
  std::thread([run_count, callback = std::move(callback)]() mutable {
    (*run_count)++;
    callback("What do you get when you multiply six by nine?", 42);
  }).detach();
}

bool bridge_construction_and_assignment() {
  BEGIN_TEST;

  // Create a new bridge.
  fit::bridge<int, const char*> bridge;
  EXPECT_TRUE(bridge.completer);
  EXPECT_TRUE(bridge.consumer);

  // Can move-construct.
  fit::bridge<int, const char*> bridge2(std::move(bridge));
  EXPECT_TRUE(bridge2.completer);
  EXPECT_TRUE(bridge2.consumer);
  EXPECT_FALSE(bridge.completer);
  EXPECT_FALSE(bridge.consumer);

  // Can move-assign.
  bridge = std::move(bridge2);
  EXPECT_TRUE(bridge.completer);
  EXPECT_TRUE(bridge.consumer);
  EXPECT_FALSE(bridge2.completer);
  EXPECT_FALSE(bridge2.consumer);

  // It still works.
  bridge.completer.complete_error("Test");
  EXPECT_FALSE(bridge.completer);
  fit::result<int, const char*> result = fit::run_single_threaded(bridge.consumer.promise());
  EXPECT_FALSE(bridge.consumer);
  EXPECT_EQ(fit::result_state::error, result.state());
  EXPECT_STR_EQ("Test", result.error());

  END_TEST;
}

bool completer_construction_and_assignment() {
  BEGIN_TEST;

  // Default constructed completer is empty.
  fit::completer<int, const char*> completer;
  EXPECT_FALSE(completer);

  // Can move-construct from non-empty.
  fit::bridge<int, const char*> bridge;
  fit::completer<int, const char*> completer2(std::move(bridge.completer));
  EXPECT_TRUE(completer2);

  // Can move-assign from non-empty.
  completer = std::move(completer2);
  EXPECT_TRUE(completer);
  EXPECT_FALSE(completer2);

  // It still works.
  completer.complete_error("Test");
  EXPECT_FALSE(completer);
  fit::result<int, const char*> result = fit::run_single_threaded(bridge.consumer.promise());
  EXPECT_FALSE(bridge.consumer);
  EXPECT_EQ(fit::result_state::error, result.state());
  EXPECT_STR_EQ("Test", result.error());

  // Can move-construct from empty.
  fit::completer<int, const char*> completer3(std::move(completer2));
  EXPECT_FALSE(completer3);
  EXPECT_FALSE(completer2);

  // Can move-assign from empty.
  completer2 = std::move(completer3);
  EXPECT_FALSE(completer2);
  EXPECT_FALSE(completer3);

  END_TEST;
}

bool completer_abandon() {
  BEGIN_TEST;

  // abandon()
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.abandon();
    EXPECT_FALSE(bridge.completer);
    EXPECT_TRUE(bridge.consumer.was_abandoned());

    fit::result<int, const char*> result =
        fit::run_single_threaded(bridge.consumer.promise_or(fit::error("Abandoned")));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fit::result_state::error, result.state());
    EXPECT_STR_EQ("Abandoned", result.error());
  }

  // completer is discarded
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer = fit::completer<int, const char*>();
    EXPECT_FALSE(bridge.completer);
    EXPECT_TRUE(bridge.consumer.was_abandoned());

    fit::result<int, const char*> result =
        fit::run_single_threaded(bridge.consumer.promise_or(fit::error("Abandoned")));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fit::result_state::error, result.state());
    EXPECT_STR_EQ("Abandoned", result.error());
  }

  END_TEST;
}

bool completer_complete() {
  BEGIN_TEST;

  // complete_ok()
  {
    fit::bridge<void, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_ok();
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<void, const char*> result = fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fit::result_state::ok, result.state());
  }

  // complete_ok(value)
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<int, const char*> result = fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fit::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // complete_error()
  {
    fit::bridge<int, void> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_error();
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<int, void> result = fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fit::result_state::error, result.state());
  }

  // complete_error(error)
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_error("Test");
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<int, const char*> result = fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fit::result_state::error, result.state());
    EXPECT_STR_EQ("Test", result.error());
  }

  // complete_or_abandon(fit::ok(...))
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_or_abandon(fit::ok(42));
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<int, const char*> result = fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fit::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // complete_or_abandon(fit::error(...))
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_or_abandon(fit::error("Test"));
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<int, const char*> result = fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fit::result_state::error, result.state());
    EXPECT_STR_EQ("Test", result.error());
  }

  // complete_or_abandon(fit::pending())
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_or_abandon(fit::pending());
    EXPECT_FALSE(bridge.completer);
    EXPECT_TRUE(bridge.consumer.was_abandoned());

    fit::result<int, const char*> result =
        fit::run_single_threaded(bridge.consumer.promise_or(fit::error("Abandoned")));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fit::result_state::error, result.state());
    EXPECT_STR_EQ("Abandoned", result.error());
  }

  END_TEST;
}

bool completer_bind_no_arg_callback() {
  BEGIN_TEST;

  // Use bind()
  {
    uint64_t run_count = 0;
    fit::bridge<> bridge;
    async_invoke_callback_no_args(&run_count, bridge.completer.bind());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<> result = fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fit::result_state::ok, result.state());
    EXPECT_EQ(1, run_count);
  }

  // Use bind_tuple()
  {
    uint64_t run_count = 0;
    fit::bridge<std::tuple<>> bridge;
    async_invoke_callback_no_args(&run_count, bridge.completer.bind_tuple());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<std::tuple<>> result = fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fit::result_state::ok, result.state());
    EXPECT_EQ(1, run_count);
  }

  END_TEST;
}

bool completer_bind_one_arg_callback() {
  BEGIN_TEST;

  // Use bind()
  {
    uint64_t run_count = 0;
    fit::bridge<std::string> bridge;
    async_invoke_callback_one_arg(&run_count, bridge.completer.bind());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<std::string> result = fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fit::result_state::ok, result.state());
    EXPECT_TRUE(result.value() == "Hippopotamus");
    EXPECT_EQ(1, run_count);
  }

  // Use bind_tuple()
  {
    uint64_t run_count = 0;
    fit::bridge<std::tuple<std::string>> bridge;
    async_invoke_callback_one_arg(&run_count, bridge.completer.bind_tuple());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<std::tuple<std::string>> result =
        fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fit::result_state::ok, result.state());
    EXPECT_TRUE(std::get<0>(result.value()) == "Hippopotamus");
    EXPECT_EQ(1, run_count);
  }

  END_TEST;
}

bool completer_bind_two_arg_callback() {
  BEGIN_TEST;

  // Use bind_tuple()
  {
    uint64_t run_count = 0;
    fit::bridge<std::tuple<std::string, int>> bridge;
    async_invoke_callback_two_args(&run_count, bridge.completer.bind_tuple());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fit::result<std::tuple<std::string, int>> result =
        fit::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fit::result_state::ok, result.state());
    EXPECT_TRUE(std::get<0>(result.value()) == "What do you get when you multiply six by nine?");
    EXPECT_EQ(42, std::get<1>(result.value()));
    EXPECT_EQ(1, run_count);
  }

  END_TEST;
}

bool consumer_construction_and_assignment() {
  BEGIN_TEST;

  // Default constructed consumer is empty.
  fit::consumer<int, const char*> consumer;
  EXPECT_FALSE(consumer);

  // Can move-construct from non-empty.
  fit::bridge<int, const char*> bridge;
  fit::consumer<int, const char*> consumer2(std::move(bridge.consumer));
  EXPECT_TRUE(consumer2);

  // Can move-assign from non-empty.
  consumer = std::move(consumer2);
  EXPECT_TRUE(consumer);
  EXPECT_FALSE(consumer2);

  // It still works.
  bridge.completer.complete_error("Test");
  EXPECT_FALSE(bridge.completer);
  fit::result<int, const char*> result = fit::run_single_threaded(consumer.promise());
  EXPECT_FALSE(consumer);
  EXPECT_EQ(fit::result_state::error, result.state());
  EXPECT_STR_EQ("Test", result.error());

  // Can move-construct from empty.
  fit::consumer<int, const char*> consumer3(std::move(consumer2));
  EXPECT_FALSE(consumer3);
  EXPECT_FALSE(consumer2);

  // Can move-assign from empty.
  consumer2 = std::move(consumer3);
  EXPECT_FALSE(consumer2);
  EXPECT_FALSE(consumer3);

  END_TEST;
}

bool consumer_cancel() {
  BEGIN_TEST;

  // cancel()
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.consumer.cancel();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);
  }

  // consumer is discarded()
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.consumer = fit::consumer<int, const char*>();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);
  }

  END_TEST;
}

bool consumer_promise() {
  BEGIN_TEST;

  // promise() when completed
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fit::promise<int, const char*> promise = bridge.consumer.promise();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);

    fit::result<int, const char*> result = fit::run_single_threaded(std::move(promise));
    EXPECT_EQ(fit::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // promise() when abandoned
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fit::promise<int, const char*> promise = bridge.consumer.promise();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.abandon();
    EXPECT_FALSE(bridge.completer);

    fit::result<int, const char*> result = fit::run_single_threaded(std::move(promise));
    EXPECT_EQ(fit::result_state::pending, result.state());
  }

  // promise_or() when completed
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fit::promise<int, const char*> promise = bridge.consumer.promise_or(fit::error("Abandoned"));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);

    fit::result<int, const char*> result = fit::run_single_threaded(std::move(promise));
    EXPECT_EQ(fit::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // promise_or() when abandoned
  {
    fit::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fit::promise<int, const char*> promise = bridge.consumer.promise_or(fit::error("Abandoned"));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.abandon();
    EXPECT_FALSE(bridge.completer);

    fit::result<int, const char*> result = fit::run_single_threaded(std::move(promise));
    EXPECT_EQ(fit::result_state::error, result.state());
    EXPECT_STR_EQ("Abandoned", result.error());
  }

  END_TEST;
}

bool schedule_for_consumer() {
  BEGIN_TEST;

  // Promise completes normally.
  {
    uint64_t run_count[2] = {};
    fit::single_threaded_executor executor;
    fit::consumer<int> consumer =
        fit::schedule_for_consumer(&executor, fit::make_promise([&](fit::context& context) {
          ASSERT_CRITICAL(context.executor() == &executor);
          run_count[0]++;
          return fit::ok(42);
        }));
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    fit::run_single_threaded(
        consumer.promise().then([&](fit::context& context, const fit::result<int>& result) {
          ASSERT_CRITICAL(context.executor() != &executor);
          ASSERT_CRITICAL(result.value() == 42);
          run_count[1]++;
        }));
    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(1, run_count[1]);
    t.join();
  }

  // Promise abandons its task so the consumer is abandoned too.
  {
    uint64_t run_count[2] = {};
    fit::single_threaded_executor executor;
    fit::consumer<int> consumer = fit::schedule_for_consumer(
        &executor, fit::make_promise([&](fit::context& context) -> fit::result<int> {
          ASSERT_CRITICAL(context.executor() == &executor);
          run_count[0]++;
          // The task will be abandoned after we return since
          // we do not acquire a susended task token for it.
          return fit::pending();
        }));
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    fit::run_single_threaded(
        consumer.promise().then([&](fit::context& context, const fit::result<int>& result) {
          // This should not run because the promise was abandoned.
          run_count[1]++;
        }));
    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(0, run_count[1]);
    t.join();
  }

  // Promise abandons its task so the consumer is abandoned too
  // but this time we use promise_or() so we can handle the abandonment.
  {
    uint64_t run_count[2] = {};
    fit::single_threaded_executor executor;
    fit::consumer<int> consumer = fit::schedule_for_consumer(
        &executor, fit::make_promise([&](fit::context& context) -> fit::result<int> {
          ASSERT_CRITICAL(context.executor() == &executor);
          run_count[0]++;
          // The task will be abandoned after we return since
          // we do not acquire a susended task token for it.
          return fit::pending();
        }));
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    fit::run_single_threaded(consumer.promise_or(fit::error())
                                 .then([&](fit::context& context, const fit::result<int>& result) {
                                   ASSERT_CRITICAL(context.executor() != &executor);
                                   ASSERT_CRITICAL(result.is_error());
                                   run_count[1]++;
                                 }));
    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(1, run_count[1]);
    t.join();
  }

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(bridge_tests)
RUN_TEST(bridge_construction_and_assignment)
RUN_TEST(completer_construction_and_assignment)
RUN_TEST(completer_abandon)
RUN_TEST(completer_complete)
RUN_TEST(completer_bind_no_arg_callback)
RUN_TEST(completer_bind_one_arg_callback)
RUN_TEST(completer_bind_two_arg_callback)
RUN_TEST(consumer_construction_and_assignment)
RUN_TEST(consumer_cancel)
RUN_TEST(consumer_promise)
RUN_TEST(schedule_for_consumer)
END_TEST_CASE(bridge_tests)
