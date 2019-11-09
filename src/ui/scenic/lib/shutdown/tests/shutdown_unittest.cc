// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/bridge.h>
#include <lib/gtest/test_loop_fixture.h>

// TODO(40804): delete once log output is properly interleaved.e
#include <chrono>
#include <thread>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/shutdown/shutdown_manager.h"

using ShutdownManagerTest = gtest::TestLoopFixture;

namespace {
constexpr zx::duration kTimeout(20000000);  // 20ms

// Provides synchronized access to test state.  This is required because the timeout callback will
// be invoked on a different thread.
class Helper {
 public:
  bool get_quit_callback_invoked() {
    std::lock_guard<std::mutex> lock(mut);
    return quit_callback_invoked;
  }

  bool get_timeout_callback_invoked() {
    std::lock_guard<std::mutex> lock(mut);
    return timeout_callback_invoked;
  }

  void set_quit_callback_invoked() {
    std::lock_guard<std::mutex> lock(mut);
    quit_callback_invoked = true;
  }

  void set_timeout_callback_invoked() {
    std::lock_guard<std::mutex> lock(mut);
    timeout_callback_invoked = true;
  }

 private:
  std::mutex mut;
  bool quit_callback_invoked = false;
  bool timeout_callback_invoked = false;
};

std::unique_ptr<scenic_impl::ShutdownManager> NewShutdownManager(async_dispatcher_t* dispatcher,
                                                                 std::shared_ptr<Helper> helper) {
  return std::make_unique<scenic_impl::ShutdownManager>(
      dispatcher,
      // |quit_callback|
      [helper] { helper->set_quit_callback_invoked(); },
      // |timeout_callback|
      [helper] { helper->set_timeout_callback_invoked(); });
}

}  // anonymous namespace

// Verify that Shutdown() succeeds when no clients are registered.
TEST_F(ShutdownManagerTest, NoClients) {
  auto helper = std::make_shared<Helper>();
  auto manager = NewShutdownManager(dispatcher(), helper);

  manager->Shutdown(kTimeout);
  EXPECT_TRUE(helper->get_quit_callback_invoked());

  // Ensure |timeout_callback| is not invoked.
  std::this_thread::sleep_for(2 * std::chrono::nanoseconds(kTimeout.get()));
  EXPECT_FALSE(helper->get_timeout_callback_invoked());
}

// Verify that Shutdown() succeeds when all registered clients shutdown before the timeout occurs.
TEST_F(ShutdownManagerTest, ClientsComplete) {
  auto helper = std::make_shared<Helper>();
  auto manager = NewShutdownManager(dispatcher(), helper);

  // Register 2 clients.  Their callback notifications won't be invoked until Shutdown() is called.
  fit::bridge<> client1, client2;
  bool client1_notified = false;
  bool client2_notified = false;
  manager->RegisterClient([&] {
    client1_notified = true;
    return client1.consumer.promise();
  });
  manager->RegisterClient([&] {
    client2_notified = true;
    return client2.consumer.promise();
  });
  EXPECT_FALSE(client1_notified);
  EXPECT_FALSE(client2_notified);
  EXPECT_FALSE(helper->get_quit_callback_invoked());

  // Initiate shutdown.  The clients should be notified immediately.
  manager->Shutdown(kTimeout);
  EXPECT_TRUE(client1_notified);
  EXPECT_TRUE(client2_notified);
  EXPECT_FALSE(helper->get_quit_callback_invoked());

  // Complete the clients' promises.  The shutdown isn't complete because the dispatcher
  // needs to "tick" in order to respond to the completion of the promises.
  client1.completer.complete_ok();
  client2.completer.complete_ok();
  EXPECT_FALSE(helper->get_quit_callback_invoked());

  bool tasks_were_run = RunLoopUntilIdle();
  EXPECT_TRUE(tasks_were_run);
  EXPECT_TRUE(helper->get_quit_callback_invoked());

  // Ensure |timeout_callback| is not invoked.
  std::this_thread::sleep_for(2 * std::chrono::nanoseconds(kTimeout.get()));
  EXPECT_FALSE(helper->get_timeout_callback_invoked());
}

// Verify that Shutdown() succeeds when some of the registered clients fail to shut down before the
// deadline.
TEST_F(ShutdownManagerTest, ClientTimesOut) {
  auto helper = std::make_shared<Helper>();
  auto manager = NewShutdownManager(dispatcher(), helper);

  // Register 2 clients.  Their callback notifications won't be invoked until Shutdown() is called.
  fit::bridge<> client1, client2;
  bool client1_notified = false;
  bool client2_notified = false;
  manager->RegisterClient([&] {
    client1_notified = true;
    return client1.consumer.promise();
  });
  manager->RegisterClient([&] {
    client2_notified = true;
    return client2.consumer.promise();
  });
  EXPECT_FALSE(client1_notified);
  EXPECT_FALSE(client2_notified);
  EXPECT_FALSE(helper->get_quit_callback_invoked());
  EXPECT_FALSE(helper->get_timeout_callback_invoked());

  // Initiate shutdown.  The clients should be notified immediately.
  manager->Shutdown(kTimeout);
  EXPECT_TRUE(client1_notified);
  EXPECT_TRUE(client2_notified);
  EXPECT_FALSE(helper->get_quit_callback_invoked());
  EXPECT_FALSE(helper->get_timeout_callback_invoked());

  // Complete only one client promise.  Because the second isn't completed, shutdown won't
  // complete until the timeout occurs.
  client1.completer.complete_ok();
  bool tasks_were_run = RunLoopUntilIdle();
  EXPECT_TRUE(tasks_were_run);
  EXPECT_FALSE(helper->get_quit_callback_invoked());
  EXPECT_FALSE(helper->get_timeout_callback_invoked());

  // Wait for up to 100x the timeout, to avoid flakes.  Typically it will require <= 2x the timeout.
  for (int i = 0; i < 100; ++i) {
    if (helper->get_timeout_callback_invoked())
      break;
    std::this_thread::sleep_for(std::chrono::nanoseconds(kTimeout.get()));
  }
  EXPECT_FALSE(helper->get_quit_callback_invoked());
  EXPECT_TRUE(helper->get_timeout_callback_invoked());
}

// Verify that nothing bad happens when some clients finish shutting down after the ShutdownManager
// has been destroyed.
TEST_F(ShutdownManagerTest, ManagerDeleted) {
  // This client outlives the ShutdownManager.
  fit::bridge<> client;
  bool client_notified = false;

  // Helper also outlives the ShutdownManager.
  auto helper = std::make_shared<Helper>();

  {
    auto manager = NewShutdownManager(dispatcher(), helper);

    manager->RegisterClient([&] {
      client_notified = true;
      return client.consumer.promise();
    });
    EXPECT_FALSE(client_notified);

    // Initiate shutdown.  The client should be notified immediately.
    manager->Shutdown(kTimeout);
    EXPECT_TRUE(client_notified);

    // Exiting the scope destroys the ShutdownManager.
  }

  client.completer.complete_ok();
  RunLoopUntilIdle();

  // Neither callback should be invoked.
  EXPECT_FALSE(helper->get_quit_callback_invoked());
  std::this_thread::sleep_for(2 * std::chrono::nanoseconds(kTimeout.get()));
  EXPECT_FALSE(helper->get_timeout_callback_invoked());
}
