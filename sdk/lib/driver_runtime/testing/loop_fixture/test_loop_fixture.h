// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_TESTING_LOOP_FIXTURE_TEST_LOOP_FIXTURE_H_
#define LIB_DRIVER_RUNTIME_TESTING_LOOP_FIXTURE_TEST_LOOP_FIXTURE_H_

#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/testing.h>
#include <lib/sync/cpp/completion.h>

#include <gtest/gtest.h>

namespace gtest {
// An extension of Test class which sets up a driver runtime message loop for
// the test.
//
// Example:
//
//   class FooTest : public ::gtest::DriverTestLoopFixture { /* ... */ };
//
//   TEST_F(FooTest, TestCase) {
//
//     // Initialize an object with the underlying driver runtime dispatcher.
//     Foo foo(driver_dispatcher());
//
//     /* Run a method on foo in the driver dispatcher */
//     RunOnDispatcher([&]() {foo.DoSomething();});
//
//     /* Wait until any posted tasks on the dispatcher are complete. */
//     WaitUntilIdle();
//
//     /* Make assertions about the state of the test case, say about |foo|. */
//   }
class DriverTestLoopFixture : public ::testing::Test {
 public:
  static void WaitUntilIdle() { fdf_testing_wait_until_all_dispatchers_idle(); }

  void SetUp() override {
    ::testing::Test::SetUp();
    // When creating a new dispatcher, we need to associate it with some owner so that the driver
    // runtime library doesn't complain.
    fdf_testing_push_driver(this);

    auto dispatcher = fdf::Dispatcher::Create(
        FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "driver-test-loop",
        [this](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown_.Signal(); });
    EXPECT_EQ(ZX_OK, dispatcher.status_value());
    dispatcher_ = std::move(dispatcher.value());

    // Now that we have created the dispatcher we can pop this.
    fdf_testing_pop_driver();
  }

  void TearDown() override {
    ::testing::Test::TearDown();
    ShutdownDriverDispatcher();
  }

  // Shuts down the driver dispatcher.
  void ShutdownDriverDispatcher() {
    dispatcher_.ShutdownAsync();
    EXPECT_EQ(ZX_OK, dispatcher_shutdown_.Wait());
  }

  // Posts a task on the driver dispatcher and waits synchronously until it is completed.
  void RunOnDispatcher(fit::closure task) {
    libsync::Completion task_completion;
    async::PostTask(dispatcher_.async_dispatcher(), [task = std::move(task), &task_completion]() {
      task();
      task_completion.Signal();
    });

    task_completion.Wait();
  }

  const fdf::Dispatcher& driver_dispatcher() { return dispatcher_; }

 private:
  fdf::Dispatcher dispatcher_;
  libsync::Completion dispatcher_shutdown_;
};

}  // namespace gtest

#endif  // LIB_DRIVER_RUNTIME_TESTING_LOOP_FIXTURE_TEST_LOOP_FIXTURE_H_
