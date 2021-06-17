// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/single_threaded_executor.h>

#include <future>
#include <string>
#include <thread>
#include <tuple>

#include <zxtest/zxtest.h>

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

TEST(BridgeTests, bridge_construction_and_assignment) {
  // Create a new bridge.
  fpromise::bridge<int, const char*> bridge;
  EXPECT_TRUE(bridge.completer);
  EXPECT_TRUE(bridge.consumer);

  // Can move-construct.
  fpromise::bridge<int, const char*> bridge2(std::move(bridge));
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
  fpromise::result<int, const char*> result =
      fpromise::run_single_threaded(bridge.consumer.promise());
  EXPECT_FALSE(bridge.consumer);
  EXPECT_EQ(fpromise::result_state::error, result.state());
  EXPECT_STR_EQ("Test", result.error());
}

TEST(BridgeTests, completer_construction_and_assignment) {
  // Default constructed completer is empty.
  fpromise::completer<int, const char*> completer;
  EXPECT_FALSE(completer);

  // Can move-construct from non-empty.
  fpromise::bridge<int, const char*> bridge;
  fpromise::completer<int, const char*> completer2(std::move(bridge.completer));
  EXPECT_TRUE(completer2);

  // Can move-assign from non-empty.
  completer = std::move(completer2);
  EXPECT_TRUE(completer);
  EXPECT_FALSE(completer2);

  // It still works.
  completer.complete_error("Test");
  EXPECT_FALSE(completer);
  fpromise::result<int, const char*> result =
      fpromise::run_single_threaded(bridge.consumer.promise());
  EXPECT_FALSE(bridge.consumer);
  EXPECT_EQ(fpromise::result_state::error, result.state());
  EXPECT_STR_EQ("Test", result.error());

  // Can move-construct from empty.
  fpromise::completer<int, const char*> completer3(std::move(completer2));
  EXPECT_FALSE(completer3);
  EXPECT_FALSE(completer2);

  // Can move-assign from empty.
  completer2 = std::move(completer3);
  EXPECT_FALSE(completer2);
  EXPECT_FALSE(completer3);
}

TEST(BridgeTests, completer_abandon) {
  // abandon()
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.abandon();
    EXPECT_FALSE(bridge.completer);
    EXPECT_TRUE(bridge.consumer.was_abandoned());

    fpromise::result<int, const char*> result =
        fpromise::run_single_threaded(bridge.consumer.promise_or(fpromise::error("Abandoned")));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_STR_EQ("Abandoned", result.error());
  }

  // completer is discarded
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer = fpromise::completer<int, const char*>();
    EXPECT_FALSE(bridge.completer);
    EXPECT_TRUE(bridge.consumer.was_abandoned());

    fpromise::result<int, const char*> result =
        fpromise::run_single_threaded(bridge.consumer.promise_or(fpromise::error("Abandoned")));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_STR_EQ("Abandoned", result.error());
  }
}

TEST(BridgeTests, completer_complete) {
  // complete_ok()
  {
    fpromise::bridge<void, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_ok();
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<void, const char*> result =
        fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
  }

  // complete_ok(value)
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<int, const char*> result =
        fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // complete_error()
  {
    fpromise::bridge<int, void> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_error();
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<int, void> result = fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fpromise::result_state::error, result.state());
  }

  // complete_error(error)
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_error("Test");
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<int, const char*> result =
        fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_STR_EQ("Test", result.error());
  }

  // complete_or_abandon(fpromise::ok(...))
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_or_abandon(fpromise::ok(42));
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<int, const char*> result =
        fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // complete_or_abandon(fpromise::error(...))
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_or_abandon(fpromise::error("Test"));
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<int, const char*> result =
        fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_STR_EQ("Test", result.error());
  }

  // complete_or_abandon(fpromise::pending())
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_or_abandon(fpromise::pending());
    EXPECT_FALSE(bridge.completer);
    EXPECT_TRUE(bridge.consumer.was_abandoned());

    fpromise::result<int, const char*> result =
        fpromise::run_single_threaded(bridge.consumer.promise_or(fpromise::error("Abandoned")));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_STR_EQ("Abandoned", result.error());
  }
}

TEST(BridgeTests, completer_bind_no_arg_callback) {
  // Use bind()
  {
    uint64_t run_count = 0;
    fpromise::bridge<> bridge;
    async_invoke_callback_no_args(&run_count, bridge.completer.bind());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<> result = fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(1, run_count);
  }

  // Use bind_tuple()
  {
    uint64_t run_count = 0;
    fpromise::bridge<std::tuple<>> bridge;
    async_invoke_callback_no_args(&run_count, bridge.completer.bind_tuple());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<std::tuple<>> result =
        fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(1, run_count);
  }
}

TEST(BridgeTests, completer_bind_one_arg_callback) {
  // Use bind()
  {
    uint64_t run_count = 0;
    fpromise::bridge<std::string> bridge;
    async_invoke_callback_one_arg(&run_count, bridge.completer.bind());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<std::string> result = fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_TRUE(result.value() == "Hippopotamus");
    EXPECT_EQ(1, run_count);
  }

  // Use bind_tuple()
  {
    uint64_t run_count = 0;
    fpromise::bridge<std::tuple<std::string>> bridge;
    async_invoke_callback_one_arg(&run_count, bridge.completer.bind_tuple());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<std::tuple<std::string>> result =
        fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_TRUE(std::get<0>(result.value()) == "Hippopotamus");
    EXPECT_EQ(1, run_count);
  }
}

TEST(BridgeTests, completer_bind_two_arg_callback) {
  // Use bind_tuple()
  {
    uint64_t run_count = 0;
    fpromise::bridge<std::tuple<std::string, int>> bridge;
    async_invoke_callback_two_args(&run_count, bridge.completer.bind_tuple());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fpromise::result<std::tuple<std::string, int>> result =
        fpromise::run_single_threaded(bridge.consumer.promise());
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_TRUE(std::get<0>(result.value()) == "What do you get when you multiply six by nine?");
    EXPECT_EQ(42, std::get<1>(result.value()));
    EXPECT_EQ(1, run_count);
  }
}

TEST(BridgeTests, consumer_construction_and_assignment) {
  // Default constructed consumer is empty.
  fpromise::consumer<int, const char*> consumer;
  EXPECT_FALSE(consumer);

  // Can move-construct from non-empty.
  fpromise::bridge<int, const char*> bridge;
  fpromise::consumer<int, const char*> consumer2(std::move(bridge.consumer));
  EXPECT_TRUE(consumer2);

  // Can move-assign from non-empty.
  consumer = std::move(consumer2);
  EXPECT_TRUE(consumer);
  EXPECT_FALSE(consumer2);

  // It still works.
  bridge.completer.complete_error("Test");
  EXPECT_FALSE(bridge.completer);
  fpromise::result<int, const char*> result = fpromise::run_single_threaded(consumer.promise());
  EXPECT_FALSE(consumer);
  EXPECT_EQ(fpromise::result_state::error, result.state());
  EXPECT_STR_EQ("Test", result.error());

  // Can move-construct from empty.
  fpromise::consumer<int, const char*> consumer3(std::move(consumer2));
  EXPECT_FALSE(consumer3);
  EXPECT_FALSE(consumer2);

  // Can move-assign from empty.
  consumer2 = std::move(consumer3);
  EXPECT_FALSE(consumer2);
  EXPECT_FALSE(consumer3);
}

TEST(BridgeTests, consumer_cancel) {
  // cancel()
  {
    fpromise::bridge<int, const char*> bridge;
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
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.consumer = fpromise::consumer<int, const char*>();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);
  }
}

TEST(BridgeTests, consumer_promise) {
  // promise() when completed
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fpromise::promise<int, const char*> promise = bridge.consumer.promise();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);

    fpromise::result<int, const char*> result = fpromise::run_single_threaded(std::move(promise));
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // promise() when abandoned
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fpromise::promise<int, const char*> promise = bridge.consumer.promise();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.abandon();
    EXPECT_FALSE(bridge.completer);

    fpromise::result<int, const char*> result = fpromise::run_single_threaded(std::move(promise));
    EXPECT_EQ(fpromise::result_state::pending, result.state());
  }

  // promise_or() when completed
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fpromise::promise<int, const char*> promise =
        bridge.consumer.promise_or(fpromise::error("Abandoned"));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);

    fpromise::result<int, const char*> result = fpromise::run_single_threaded(std::move(promise));
    EXPECT_EQ(fpromise::result_state::ok, result.state());
    EXPECT_EQ(42, result.value());
  }

  // promise_or() when abandoned
  {
    fpromise::bridge<int, const char*> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fpromise::promise<int, const char*> promise =
        bridge.consumer.promise_or(fpromise::error("Abandoned"));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.abandon();
    EXPECT_FALSE(bridge.completer);

    fpromise::result<int, const char*> result = fpromise::run_single_threaded(std::move(promise));
    EXPECT_EQ(fpromise::result_state::error, result.state());
    EXPECT_STR_EQ("Abandoned", result.error());
  }
}

TEST(BridgeTests, schedule_for_consumer) {
  // Promise completes normally.
  {
    uint64_t run_count[2] = {};
    fpromise::single_threaded_executor executor;
    fpromise::consumer<int> consumer = fpromise::schedule_for_consumer(
        &executor, fpromise::make_promise([&](fpromise::context& context) {
          ASSERT_CRITICAL(context.executor() == &executor);
          run_count[0]++;
          return fpromise::ok(42);
        }));
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    fpromise::run_single_threaded(consumer.promise().then(
        [&](fpromise::context& context, const fpromise::result<int>& result) {
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
    fpromise::single_threaded_executor executor;
    fpromise::consumer<int> consumer = fpromise::schedule_for_consumer(
        &executor, fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<int> {
          ASSERT_CRITICAL(context.executor() == &executor);
          run_count[0]++;
          // The task will be abandoned after we return since
          // we do not acquire a susended task token for it.
          return fpromise::pending();
        }));
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    fpromise::run_single_threaded(consumer.promise().then(
        [&](fpromise::context& context, const fpromise::result<int>& result) {
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
    fpromise::single_threaded_executor executor;
    fpromise::consumer<int> consumer = fpromise::schedule_for_consumer(
        &executor, fpromise::make_promise([&](fpromise::context& context) -> fpromise::result<int> {
          ASSERT_CRITICAL(context.executor() == &executor);
          run_count[0]++;
          // The task will be abandoned after we return since
          // we do not acquire a susended task token for it.
          return fpromise::pending();
        }));
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    fpromise::run_single_threaded(
        consumer.promise_or(fpromise::error())
            .then([&](fpromise::context& context, const fpromise::result<int>& result) {
              ASSERT_CRITICAL(context.executor() != &executor);
              ASSERT_CRITICAL(result.is_error());
              run_count[1]++;
            }));
    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(1, run_count[1]);
    t.join();
  }
}

}  // namespace
