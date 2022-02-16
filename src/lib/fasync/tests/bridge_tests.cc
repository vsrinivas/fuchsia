// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/bridge.h>
#include <lib/fasync/future.h>
#include <lib/fasync/single_threaded_executor.h>

#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>

#include <zxtest/zxtest.h>

namespace {

using continuation = fasync::internal::bridge_state<const char*>::future_continuation;
static_assert(fasync::is_future_v<continuation>);
static_assert(cpp17::is_same_v<fasync::future_output_t<continuation>, fitx::result<const char*>>);

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
  fasync::bridge<const char*, int> bridge;
  EXPECT_TRUE(bridge.completer);
  EXPECT_TRUE(bridge.consumer);

  // Can move-construct.
  fasync::bridge<const char*, int> bridge2(std::move(bridge));
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
  fitx::result<const char*, int> result = fasync::block(bridge.consumer.future()).value();
  EXPECT_FALSE(bridge.consumer);
  EXPECT_TRUE(result.is_error());
  EXPECT_STREQ("Test", result.error_value());
}

TEST(BridgeTests, completer_construction_and_assignment) {
  // Default constructed completer is empty.
  fasync::completer<const char*, int> completer;
  EXPECT_FALSE(completer);

  // Can move-construct from non-empty.
  fasync::bridge<const char*, int> bridge;
  fasync::completer<const char*, int> completer2(std::move(bridge.completer));
  EXPECT_TRUE(completer2);

  // Can move-assign from non-empty.
  completer = std::move(completer2);
  EXPECT_TRUE(completer);
  EXPECT_FALSE(completer2);

  // It still works.
  completer.complete_error("Test");
  EXPECT_FALSE(completer);
  fitx::result<const char*, int> result = fasync::block(bridge.consumer.future()).value();
  EXPECT_FALSE(bridge.consumer);
  EXPECT_TRUE(result.is_error());
  EXPECT_STREQ("Test", result.error_value());

  // Can move-construct from empty.
  fasync::completer<const char*, int> completer3(std::move(completer2));
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
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.abandon();
    EXPECT_FALSE(bridge.completer);
    EXPECT_TRUE(bridge.consumer.was_abandoned());

    fitx::result<const char*, int> result =
        fasync::block(bridge.consumer.future_or(fitx::error("Abandoned"))).value();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(result.is_error());
    EXPECT_STREQ("Abandoned", result.error_value());
  }

  // completer is discarded
  {
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer = fasync::completer<const char*, int>();
    EXPECT_FALSE(bridge.completer);
    EXPECT_TRUE(bridge.consumer.was_abandoned());

    fitx::result<const char*, int> result =
        fasync::block(bridge.consumer.future_or(fitx::error("Abandoned"))).value();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(result.is_error());
    EXPECT_STREQ("Abandoned", result.error_value());
  }
}

TEST(BridgeTests, completer_complete) {
  // complete_ok()
  {
    fasync::bridge<const char*> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_ok();
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<const char*> result = fasync::block(bridge.consumer.future()).value();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(result.is_ok());
  }

  // complete_ok(value)
  {
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<const char*, int> result = fasync::block(bridge.consumer.future()).value();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(42, result.value());
  }

  // complete_error()
  {
    fasync::bridge<fitx::failed, int> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_error();
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<fitx::failed, int> result = fasync::block(bridge.consumer.future()).value();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(result.is_error());
  }

  // complete_error(error)
  {
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete_error("Test");
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<const char*, int> result = fasync::block(bridge.consumer.future()).value();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(result.is_error());
    EXPECT_STREQ("Test", result.error_value());
  }

  // complete(fitx::ok(...))
  {
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete(fitx::ok(42));
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<const char*, int> result = fasync::block(bridge.consumer.future()).value();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(42, result.value());
  }

  // complete(fitx::error(...))
  {
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    bridge.completer.complete(fitx::error("Test"));
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<const char*, int> result = fasync::block(bridge.consumer.future()).value();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(result.is_error());
    EXPECT_STREQ("Test", result.error_value());
  }
}

TEST(BridgeTests, completer_bind_no_arg_callback) {
  // Use bind()
  {
    uint64_t run_count = 0;
    fasync::bridge<fitx::failed> bridge;
    async_invoke_callback_no_args(&run_count, bridge.completer.bind());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<fitx::failed> result = fasync::block(bridge.consumer.future()).value();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(1, run_count);
  }

  // Use tuple bind()
  {
    uint64_t run_count = 0;
    fasync::bridge<fitx::failed, std::tuple<>> bridge;
    async_invoke_callback_no_args(&run_count, bridge.completer.bind());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<fitx::failed, std::tuple<>> result =
        fasync::block(bridge.consumer.future()).value();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(1, run_count);
  }
}

TEST(BridgeTests, completer_bind_one_arg_callback) {
  // Use bind()
  {
    uint64_t run_count = 0;
    fasync::bridge<fitx::failed, std::string> bridge;
    async_invoke_callback_one_arg(&run_count, bridge.completer.bind());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<fitx::failed, std::string> result =
        fasync::block(bridge.consumer.future()).value();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), "Hippopotamus");
    EXPECT_EQ(1, run_count);
  }

  // Use tuple bind()
  {
    uint64_t run_count = 0;
    fasync::bridge<fitx::failed, std::tuple<std::string>> bridge;
    async_invoke_callback_one_arg(&run_count, bridge.completer.bind());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<fitx::failed, std::tuple<std::string>> result =
        fasync::block(bridge.consumer.future()).value();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(std::get<0>(result.value()), "Hippopotamus");
    EXPECT_EQ(1, run_count);
  }
}

TEST(BridgeTests, completer_bind_two_arg_callback) {
  // Use tuple bind()
  {
    uint64_t run_count = 0;
    fasync::bridge<fitx::failed, std::tuple<std::string, int>> bridge;
    async_invoke_callback_two_args(&run_count, bridge.completer.bind());
    EXPECT_FALSE(bridge.completer);
    EXPECT_FALSE(bridge.consumer.was_abandoned());

    fitx::result<fitx::failed, std::tuple<std::string, int>> result =
        fasync::block(bridge.consumer.future()).value();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(std::get<0>(result.value()), "What do you get when you multiply six by nine?");
    EXPECT_EQ(42, std::get<1>(result.value()));
    EXPECT_EQ(1, run_count);
  }
}

TEST(BridgeTests, consumer_construction_and_assignment) {
  // Default constructed consumer is empty.
  fasync::consumer<const char*, int> consumer;
  EXPECT_FALSE(consumer);

  // Can move-construct from non-empty.
  fasync::bridge<const char*, int> bridge;
  fasync::consumer<const char*, int> consumer2(std::move(bridge.consumer));
  EXPECT_TRUE(consumer2);

  // Can move-assign from non-empty.
  consumer = std::move(consumer2);
  EXPECT_TRUE(consumer);
  EXPECT_FALSE(consumer2);

  // It still works.
  bridge.completer.complete_error("Test");
  EXPECT_FALSE(bridge.completer);
  fitx::result<const char*, int> result = fasync::block(consumer.future()).value();
  EXPECT_FALSE(consumer);
  EXPECT_TRUE(result.is_error());
  EXPECT_STREQ("Test", result.error_value());

  // Can move-construct from empty.
  fasync::consumer<const char*, int> consumer3(std::move(consumer2));
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
    fasync::bridge<const char*, int> bridge;
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
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.consumer = fasync::consumer<const char*, int>();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_TRUE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);
  }
}

TEST(BridgeTests, consumer_future) {
  // future() when completed
  {
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fasync::try_future<const char*, int> future = bridge.consumer.future();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);

    fitx::result<const char*, int> result = fasync::block(std::move(future)).value();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(42, result.value());
  }

  // future() when abandoned
  {
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fasync::try_future<const char*, int> future = bridge.consumer.future();
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.abandon();
    EXPECT_FALSE(bridge.completer);

    cpp17::optional<fitx::result<const char*, int>> result = fasync::block(std::move(future));
    EXPECT_FALSE(result.has_value());
  }

  // future_or() when completed
  {
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fasync::try_future<const char*, int> future =
        bridge.consumer.future_or(fitx::error("Abandoned"));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.complete_ok(42);
    EXPECT_FALSE(bridge.completer);

    fitx::result<const char*, int> result = fasync::block(std::move(future)).value();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(42, result.value());
  }

  // future_or() when abandoned
  {
    fasync::bridge<const char*, int> bridge;
    EXPECT_TRUE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    fasync::try_future<const char*, int> future =
        bridge.consumer.future_or(fitx::error("Abandoned"));
    EXPECT_FALSE(bridge.consumer);
    EXPECT_FALSE(bridge.completer.was_canceled());

    bridge.completer.abandon();
    EXPECT_FALSE(bridge.completer);

    fitx::result<const char*, int> result = fasync::block(std::move(future)).value();
    EXPECT_TRUE(result.is_error());
    EXPECT_STREQ("Abandoned", result.error_value());
  }
}

TEST(BridgeTests, schedule_for_consumer) {
  // Future completes normally.
  {
    uint64_t run_count[2] = {};
    fasync::single_threaded_executor executor;
    fasync::consumer<fitx::failed, int> consumer = fasync::schedule_for_consumer(
        fasync::make_future([&](fasync::context& context) -> fitx::result<fitx::failed, int> {
          EXPECT_EQ(&context.executor(), &executor);
          run_count[0]++;
          return fitx::ok(42);
        }),
        executor);
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    consumer.future() |
        fasync::then([&](fasync::context& context, const fitx::result<fitx::failed, int>& result) {
          ASSERT_NE(&context.executor(), &executor);
          ASSERT_EQ(result.value(), 42);
          run_count[1]++;
        }) |
        fasync::block;
    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(1, run_count[1]);
    t.join();
  }

  // Future abandons its task so the consumer is abandoned too.
  {
    uint64_t run_count[2] = {};
    fasync::single_threaded_executor executor;
    fasync::consumer<fitx::failed, int> consumer = fasync::schedule_for_consumer(
        fasync::make_future([&](fasync::context& context) -> fasync::try_poll<fitx::failed, int> {
          EXPECT_EQ(&context.executor(), &executor);
          run_count[0]++;
          // The task will be abandoned after we return since
          // we do not acquire a susended task token for it.
          return fasync::pending();
        }),
        executor);
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    consumer.future() |
        fasync::then([&](fasync::context& context, const fitx::result<fitx::failed, int>& result) {
          // This should not run because the future was abandoned.
          run_count[1]++;
        }) |
        fasync::block;
    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(0, run_count[1]);
    t.join();
  }

  // Future abandons its task so the consumer is abandoned too
  // but this time we use future_or() so we can handle the abandonment.
  {
    uint64_t run_count[2] = {};
    fasync::single_threaded_executor executor;
    fasync::consumer<fitx::failed, int> consumer = fasync::schedule_for_consumer(
        fasync::make_future([&](fasync::context& context) -> fasync::try_poll<fitx::failed, int> {
          EXPECT_EQ(&context.executor(), &executor);
          run_count[0]++;
          // The task will be abandoned after we return since
          // we do not acquire a susended task token for it.
          return fasync::pending();
        }),
        executor);
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    consumer.future_or(fitx::failed()) |
        fasync::then([&](fasync::context& context, const fitx::result<fitx::failed, int>& result) {
          ASSERT_NE(&context.executor(), &executor);
          ASSERT_TRUE(result.is_error());
          run_count[1]++;
        }) |
        fasync::block;
    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(1, run_count[1]);
    t.join();
  }
}

TEST(BridgeTests, split) {
  // Future completes normally.
  {
    uint64_t run_count[2] = {};
    fasync::single_threaded_executor executor;
    auto future =
        fasync::make_future([&](fasync::context& context) -> fitx::result<fitx::failed, int> {
          EXPECT_EQ(&context.executor(), &executor);
          run_count[0]++;
          return fitx::ok(42);
        }) |
        fasync::split(executor) |
        fasync::then([&](fasync::context& context, const fitx::result<fitx::failed, int>& result) {
          ASSERT_NE(&context.executor(), &executor);
          ASSERT_EQ(result.value(), 42);
          run_count[1]++;
        });
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    std::move(future) | fasync::block;
    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(1, run_count[1]);
    t.join();
  }

  // Future abandons its task so the chained future is abandoned too.
  {
    uint64_t run_count[2] = {};
    fasync::single_threaded_executor executor;
    auto future =
        fasync::make_future([&](fasync::context& context) -> fasync::try_poll<fitx::failed, int> {
          EXPECT_EQ(&context.executor(), &executor);
          run_count[0]++;
          // The task will be abandoned after we return since
          // we do not acquire a susended task token for it.
          return fasync::pending();
        }) |
        fasync::split(executor) |
        fasync::then([&](fasync::context& context, const fitx::result<fitx::failed, int>& result) {
          // This should not run because the future was abandoned.
          run_count[1]++;
        });
    EXPECT_EQ(0, run_count[0]);

    auto t = std::thread([&] { executor.run(); });
    std::move(future) | fasync::block;
    EXPECT_EQ(1, run_count[0]);
    EXPECT_EQ(0, run_count[1]);
    t.join();
  }
}

}  // namespace
