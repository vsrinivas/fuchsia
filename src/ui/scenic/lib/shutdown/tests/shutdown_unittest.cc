// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/bridge.h>
#include <lib/gtest/test_loop_fixture.h>

// TODO(fxbug.dev/40804): delete once log output is properly interleaved.e
#include <lib/syslog/cpp/macros.h>

#include <chrono>
#include <thread>

#include "src/ui/scenic/lib/shutdown/shutdown_manager.h"

namespace {

class ShutdownManagerTest : public gtest::TestLoopFixture {
 public:
  // Provides synchronized access to test state.  This is required because the timeout callback will
  // be invoked on a different thread.
  class State {
   public:
    bool get_quit_callback_invoked() {
      std::lock_guard<std::mutex> lock(mut);
      return quit_callback_invoked;
    }

    bool get_timeout_callback_invoked() {
      std::lock_guard<std::mutex> lock(mut);
      return timeout_callback_invoked;
    }

    bool get_timeout_callback_invocation_value() {
      std::lock_guard<std::mutex> lock(mut);
      return timeout_callback_invocation_value;
    }

    void set_quit_callback_invoked() {
      std::lock_guard<std::mutex> lock(mut);
      quit_callback_invoked = true;
    }

    void set_timeout_callback_invoked(bool value) {
      std::lock_guard<std::mutex> lock(mut);
      timeout_callback_invoked = true;
      timeout_callback_invocation_value = value;
    }

   private:
    std::mutex mut;
    bool quit_callback_invoked = false;
    bool timeout_callback_invoked = false;
    bool timeout_callback_invocation_value = false;
  };

 protected:
  void SetUp() {
    gtest::TestLoopFixture::SetUp();

    clock_time_ = std::make_shared<std::atomic<zx::time>>();
    UpdateTimeoutClock();

    state_ = std::make_shared<State>();

    manager_ = scenic_impl::ShutdownManager::New(
        dispatcher(),
        // |quit_callback|
        [state = state_] { state->set_quit_callback_invoked(); },
        // |timeout_callback|
        [state = state_](bool timed_out) { state->set_timeout_callback_invoked(timed_out); });

    manager_->set_clock_callback([clock = clock_time_] { return clock->load(); });
  }

  void TearDown() {
    manager_.reset();
    state_.reset();
    clock_time_.reset();

    gtest::TestLoopFixture::TearDown();
  }

  // Same as superclass, plus updates the timeout-clock time.
  bool RunLoopUntil(zx::time deadline) {
    auto result = gtest::TestLoopFixture::RunLoopUntil(deadline);
    UpdateTimeoutClock();
    return result;
  }

  // Same as superclass, plus updates the timeout-clock time.
  bool RunLoopFor(zx::duration duration) {
    auto result = gtest::TestLoopFixture::RunLoopFor(duration);
    UpdateTimeoutClock();
    return result;
  }

  // Same as superclass, plus updates the timeout-clock time.
  bool RunLoopUntilIdle() {
    auto result = gtest::TestLoopFixture::RunLoopUntilIdle();
    UpdateTimeoutClock();
    return result;
  }

  const std::shared_ptr<scenic_impl::ShutdownManager>& manager() { return manager_; }
  void ResetManager() { manager_.reset(); }
  State* state() { return state_.get(); }

  void WaitForTimeoutInvocation() {
    while (!state()->get_timeout_callback_invoked()) {
      std::this_thread::yield();
    }
  }

 private:
  void UpdateTimeoutClock() {
    FX_CHECK(clock_time_);
    clock_time_->store(Now());
  }

  std::shared_ptr<State> state_;
  std::shared_ptr<scenic_impl::ShutdownManager> manager_;
  std::shared_ptr<std::atomic<zx::time>> clock_time_;
};

constexpr zx::duration kTimeout(20000000);  // 20ms

}  // anonymous namespace

// Verify that Shutdown() succeeds when no clients are registered.
TEST_F(ShutdownManagerTest, NoClients) {
  manager()->Shutdown(kTimeout);

  EXPECT_TRUE(state()->get_quit_callback_invoked());
  EXPECT_TRUE(state()->get_timeout_callback_invoked());
  EXPECT_FALSE(state()->get_timeout_callback_invocation_value());
}

// Verify that Shutdown() succeeds when all registered clients shutdown before the timeout occurs.
TEST_F(ShutdownManagerTest, ClientsComplete) {
  // Register 2 clients.  Their callback notifications won't be invoked until Shutdown() is called.
  fit::bridge<> client1, client2;
  bool client1_notified = false;
  bool client2_notified = false;
  manager()->RegisterClient([&] {
    client1_notified = true;
    return client1.consumer.promise();
  });
  manager()->RegisterClient([&] {
    client2_notified = true;
    return client2.consumer.promise();
  });
  EXPECT_FALSE(client1_notified);
  EXPECT_FALSE(client2_notified);
  EXPECT_FALSE(state()->get_quit_callback_invoked());

  // Initiate shutdown.  The clients should be notified immediately.
  manager()->Shutdown(kTimeout);
  EXPECT_TRUE(client1_notified);
  EXPECT_TRUE(client2_notified);
  EXPECT_FALSE(state()->get_quit_callback_invoked());

  // Complete the clients' promises.  The shutdown isn't complete because the dispatcher
  // needs to "tick" in order to respond to the completion of the promises.
  client1.completer.complete_ok();
  client2.completer.complete_ok();
  EXPECT_FALSE(state()->get_quit_callback_invoked());

  bool tasks_were_run = RunLoopUntilIdle();
  EXPECT_TRUE(tasks_were_run);
  EXPECT_TRUE(state()->get_quit_callback_invoked());

  RunLoopFor(kTimeout);

  WaitForTimeoutInvocation();
  EXPECT_FALSE(state()->get_timeout_callback_invocation_value());
}

// Verify that Shutdown() succeeds when some of the registered clients fail to shut down before the
// deadline.
TEST_F(ShutdownManagerTest, ClientTimesOut) {
  // Register 2 clients.  Their callback notifications won't be invoked until Shutdown() is called.
  fit::bridge<> client1, client2;
  bool client1_notified = false;
  bool client2_notified = false;
  manager()->RegisterClient([&] {
    client1_notified = true;
    return client1.consumer.promise();
  });
  manager()->RegisterClient([&] {
    client2_notified = true;
    return client2.consumer.promise();
  });
  EXPECT_FALSE(client1_notified);
  EXPECT_FALSE(client2_notified);
  EXPECT_FALSE(state()->get_quit_callback_invoked());
  EXPECT_FALSE(state()->get_timeout_callback_invoked());

  // Initiate shutdown.  The clients should be notified immediately.
  manager()->Shutdown(kTimeout);
  EXPECT_TRUE(client1_notified);
  EXPECT_TRUE(client2_notified);
  EXPECT_FALSE(state()->get_quit_callback_invoked());
  EXPECT_FALSE(state()->get_timeout_callback_invoked());

  // Complete only one client promise.  Because the second isn't completed, shutdown won't
  // complete until the timeout occurs.
  client1.completer.complete_ok();
  bool tasks_were_run = RunLoopUntilIdle();
  EXPECT_TRUE(tasks_were_run);
  EXPECT_FALSE(state()->get_quit_callback_invoked());
  EXPECT_FALSE(state()->get_timeout_callback_invoked());

  RunLoopFor(kTimeout);

  WaitForTimeoutInvocation();
  EXPECT_FALSE(state()->get_quit_callback_invoked());
  EXPECT_TRUE(state()->get_timeout_callback_invocation_value());
}

// Verify that nothing bad happens when some clients finish shutting down after the ShutdownManager
// has been destroyed.
TEST_F(ShutdownManagerTest, ManagerDeleted) {
  // This client outlives the ShutdownManager.
  fit::bridge<> client;
  bool client_notified = false;

  {
    manager()->RegisterClient([&] {
      client_notified = true;
      return client.consumer.promise();
    });
    EXPECT_FALSE(client_notified);

    // Initiate shutdown.  The client should be notified immediately.
    manager()->Shutdown(kTimeout);
    EXPECT_TRUE(client_notified);

    // Verify that timeout thread doesn't hang onto the manager.
    std::weak_ptr<scenic_impl::ShutdownManager> weak(manager());
    ResetManager();
    EXPECT_FALSE(weak.lock());
  }

  client.completer.complete_ok();
  RunLoopUntilIdle();

  WaitForTimeoutInvocation();
  EXPECT_FALSE(state()->get_quit_callback_invoked());
  EXPECT_FALSE(state()->get_timeout_callback_invocation_value());
}
