// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/blocking_callback_waiter.h"

#include <lib/fit/function.h>

#include <memory>

#include "gtest/gtest.h"

namespace ledger {
namespace {

class FakeLoopController : public LoopController {
 public:
  FakeLoopController(fit::function<void()> on_run, fit::function<void()> on_stop)
      : on_run_(std::move(on_run)), on_stop_(std::move(on_stop)) {}
  FakeLoopController(const FakeLoopController&) = delete;
  FakeLoopController& operator=(const FakeLoopController&) = delete;

  void RunLoop() override { on_run_(); };

  void StopLoop() override { on_stop_(); };

  std::unique_ptr<SubLoop> StartNewLoop() override {
    FXL_NOTREACHED();
    return nullptr;
  }

  std::unique_ptr<CallbackWaiter> NewWaiter() override {
    return std::make_unique<BlockingCallbackWaiter>(this);
  }

  async_dispatcher_t* dispatcher() override {
    FXL_NOTREACHED();
    return nullptr;
  }

  bool RunLoopUntil(fit::function<bool()> /* condition */) override {
    FXL_NOTREACHED();
    return false;
  }

  void RunLoopFor(zx::duration /* duration */) override { FXL_NOTREACHED(); }

 private:
  fit::function<void()> on_run_;
  fit::function<void()> on_stop_;
};

TEST(BlockingCallbackWaiterTest, PreCall) {
  size_t nb_run = 0;
  size_t nb_stop = 0;
  FakeLoopController loop_controller([&] { ++nb_run; }, [&] { ++nb_stop; });

  auto waiter = loop_controller.NewWaiter();
  auto callback = waiter->GetCallback();

  callback();
  ASSERT_TRUE(waiter->RunUntilCalled());

  EXPECT_EQ(nb_run, 0u);
  EXPECT_EQ(nb_stop, 0u);
}

TEST(BlockingCallbackWaiterTest, MultipleGetCallback) {
  size_t nb_run = 0;
  size_t nb_stop = 0;
  FakeLoopController loop_controller([&] { ++nb_run; }, [&] { ++nb_stop; });

  auto waiter = loop_controller.NewWaiter();

  waiter->GetCallback()();
  waiter->GetCallback()();

  ASSERT_TRUE(waiter->RunUntilCalled());
  ASSERT_TRUE(waiter->RunUntilCalled());

  EXPECT_EQ(nb_run, 0u);
  EXPECT_EQ(nb_stop, 0u);
}

TEST(BlockingCallbackWaiterTest, PostCall) {
  size_t nb_run = 0;
  size_t nb_stop = 0;
  std::unique_ptr<fit::closure> callback;
  FakeLoopController loop_controller(
      [&] {
        ++nb_run;
        if (callback) {
          (*callback)();
        }
      },
      [&] { ++nb_stop; });

  auto waiter = loop_controller.NewWaiter();
  callback = std::make_unique<fit::closure>(waiter->GetCallback());

  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_EQ(nb_run, 1u);
  EXPECT_EQ(nb_stop, 1u);

  // loop_controller must outlive the waiter.
  callback.reset();
}

TEST(BlockingCallbackWaiterTest, MultipleRunUntilCalled) {
  size_t nb_run = 0;
  size_t nb_stop = 0;
  std::unique_ptr<fit::closure> callback;
  FakeLoopController loop_controller(
      [&] {
        ++nb_run;
        if (callback) {
          (*callback)();
        }
      },
      [&] { ++nb_stop; });

  auto waiter = loop_controller.NewWaiter();
  callback = std::make_unique<fit::closure>(waiter->GetCallback());

  ASSERT_TRUE(waiter->RunUntilCalled());
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_EQ(nb_run, 2u);
  EXPECT_EQ(nb_stop, 2u);

  // loop_controller must outlive the waiter.
  callback.reset();
}

TEST(BlockingCallbackWaiterTest, InterleaveRunUntilCalledAndCall) {
  size_t nb_run = 0;
  size_t nb_stop = 0;
  std::unique_ptr<fit::closure> callback;
  FakeLoopController loop_controller(
      [&] {
        ++nb_run;
        if (callback) {
          (*callback)();
        }
      },
      [&] { ++nb_stop; });

  auto waiter = loop_controller.NewWaiter();
  callback = std::make_unique<fit::closure>(waiter->GetCallback());

  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_EQ(nb_run, 1u);
  EXPECT_EQ(nb_stop, 1u);

  (*callback)();
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_EQ(nb_run, 1u);
  EXPECT_EQ(nb_stop, 1u);

  // loop_controller must outlive the waiter.
  callback.reset();
}

TEST(BlockingCallbackWaiterTest, NotCalledYet) {
  FakeLoopController loop_controller([] {}, [] {});
  auto waiter = loop_controller.NewWaiter();
  auto callback = waiter->GetCallback();

  EXPECT_TRUE(waiter->NotCalledYet());

  callback();
  EXPECT_FALSE(waiter->NotCalledYet());
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_TRUE(waiter->NotCalledYet());

  callback();
  callback();
  EXPECT_FALSE(waiter->NotCalledYet());
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_FALSE(waiter->NotCalledYet());
  ASSERT_TRUE(waiter->RunUntilCalled());
  EXPECT_TRUE(waiter->NotCalledYet());
}

TEST(BlockingCallbackWaiterTest, FailedWhenNoCallbackIsAlive) {
  std::unique_ptr<fit::closure> on_run_callback;
  FakeLoopController loop_controller(
      [&] {
        if (on_run_callback) {
          (*on_run_callback)();
        }
      },
      [] {});
  auto waiter = loop_controller.NewWaiter();

  EXPECT_FALSE(waiter->RunUntilCalled());

  fit::closure callback = waiter->GetCallback();
  callback = nullptr;
  EXPECT_FALSE(waiter->RunUntilCalled());

  callback = waiter->GetCallback();
  on_run_callback = std::make_unique<fit::closure>([&] { callback = nullptr; });
  EXPECT_FALSE(waiter->RunUntilCalled());
}

}  // namespace
}  // namespace ledger
