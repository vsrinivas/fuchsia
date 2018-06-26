// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"

#include <memory>

#include <lib/fit/function.h>

#include "gtest/gtest.h"

namespace test {
namespace {

class FakeLoopController : public LedgerAppInstanceFactory::LoopController {
 public:
  FakeLoopController(fit::function<void()> on_run,
                     fit::function<void()> on_stop)
      : on_run_(std::move(on_run)), on_stop_(std::move(on_stop)) {}
  FakeLoopController(const FakeLoopController&) = delete;
  FakeLoopController& operator=(const FakeLoopController&) = delete;

  void RunLoop() override { on_run_(); };

  void StopLoop() override { on_stop_(); };

 private:
  fit::function<void()> on_run_;
  fit::function<void()> on_stop_;
};

TEST(CallbackWaiterTest, PreCall) {
  size_t nb_run = 0;
  size_t nb_stop = 0;
  FakeLoopController loop_controller([&] { ++nb_run; }, [&] { ++nb_stop; });

  auto waiter = loop_controller.NewWaiter();
  auto callback = waiter->GetCallback();

  callback();
  waiter->RunUntilCalled();

  EXPECT_EQ(0u, nb_run);
  EXPECT_EQ(0u, nb_stop);
}

TEST(CallbackWaiterTest, MultipleGetCallback) {
  size_t nb_run = 0;
  size_t nb_stop = 0;
  FakeLoopController loop_controller([&] { ++nb_run; }, [&] { ++nb_stop; });

  auto waiter = loop_controller.NewWaiter();

  waiter->GetCallback()();
  waiter->GetCallback()();

  waiter->RunUntilCalled();
  waiter->RunUntilCalled();

  EXPECT_EQ(0u, nb_run);
  EXPECT_EQ(0u, nb_stop);
}

TEST(CallbackWaiterTest, PostCall) {
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

  waiter->RunUntilCalled();
  EXPECT_EQ(1u, nb_run);
  EXPECT_EQ(1u, nb_stop);
}

TEST(CallbackWaiterTest, MultipleRunUntilCalled) {
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

  waiter->RunUntilCalled();
  waiter->RunUntilCalled();
  EXPECT_EQ(2u, nb_run);
  EXPECT_EQ(2u, nb_stop);
}

TEST(CallbackWaiterTest, InterleaveRunUntilCalledAndCall) {
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

  waiter->RunUntilCalled();
  EXPECT_EQ(1u, nb_run);
  EXPECT_EQ(1u, nb_stop);

  (*callback)();
  waiter->RunUntilCalled();
  EXPECT_EQ(1u, nb_run);
  EXPECT_EQ(1u, nb_stop);
}

TEST(CallbackWaiterTest, NotCalledYet) {
  FakeLoopController loop_controller([] {}, [] {});
  auto waiter = loop_controller.NewWaiter();
  auto callback = waiter->GetCallback();

  EXPECT_TRUE(waiter->NotCalledYet());

  callback();
  EXPECT_FALSE(waiter->NotCalledYet());
  waiter->RunUntilCalled();
  EXPECT_TRUE(waiter->NotCalledYet());

  callback();
  callback();
  EXPECT_FALSE(waiter->NotCalledYet());
  waiter->RunUntilCalled();
  EXPECT_FALSE(waiter->NotCalledYet());
  waiter->RunUntilCalled();
  EXPECT_TRUE(waiter->NotCalledYet());
}

}  // namespace
}  // namespace test
