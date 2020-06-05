// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reboot_watcher_manager.h"

#include <fuchsia/hardware/power/statecontrol/llcpp/fidl.h>
#include <lib/async-testing/test_loop.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include <zxtest/zxtest.h>

#include "reboot_watcher_manager_test_utils.h"

using namespace llcpp::fuchsia::hardware::power::statecontrol;

constexpr RebootReason kRebootReason{RebootReason::USER_REQUEST};
constexpr size_t kNumWatchers = 5u;
constexpr zx::duration kWatchdogTimeout = zx::sec(MAX_REBOOT_WATCHER_RESPONSE_TIME_SECONDS);

class RebootWatcherManagerTestCase : public zxtest::Test {
 public:
  RebootWatcherManagerTestCase() : watcher_manager_(dispatcher()) {}

 protected:
  async_dispatcher* dispatcher() { return loop_.dispatcher(); }
  async::TestLoop* loop() { return &loop_; }

  RebootWatcherManager& watcher_manager() { return watcher_manager_; }

 private:
  async::TestLoop loop_;
  RebootWatcherManager watcher_manager_;
};

TEST_F(RebootWatcherManagerTestCase, Check_AddAndRemoveWatchers) {
  std::vector<zx::channel> watcher_remotes(kNumWatchers);

  for (auto& watcher_remote : watcher_remotes) {
    zx::channel watcher;
    ASSERT_OK(zx::channel::create(0, &watcher, &watcher_remote));

    // The completer can be instantiated with a null transaction because the completer is unused in
    // |RebootWatcherManager|.
    watcher_manager().Register(
        std::move(watcher),
        RebootMethodsWatcherRegister::Interface::RegisterCompleter::Sync(nullptr));
  }

  EXPECT_EQ(watcher_manager().NumWatchers(), kNumWatchers);

  while (!watcher_remotes.empty()) {
    watcher_remotes.pop_back();
    loop()->RunUntilIdle();
    if (watcher_remotes.empty()) {
      EXPECT_EQ(watcher_manager().NumWatchers(), 0u);
    } else {
      EXPECT_GE(watcher_manager().NumWatchers(), 0u);
    }
  }
}

TEST_F(RebootWatcherManagerTestCase, Check_NotifyAll) {
  std::vector<MockRebootMethodsWatcher> watchers(kNumWatchers);

  for (auto& watcher : watchers) {
    zx::channel watcher_channel;
    zx::channel watcher_remote_channel;
    ASSERT_OK(zx::channel::create(0, &watcher_channel, &watcher_remote_channel));

    // The completer can be instantiated with a null transaction because the completer is unused in
    // |RebootWatcherManager|.
    watcher_manager().Register(
        std::move(watcher_channel),
        RebootMethodsWatcherRegister::Interface::RegisterCompleter::Sync(nullptr));
    fidl::Bind(dispatcher(), std::move(watcher_remote_channel), &watcher);
  }

  watcher_manager().SetRebootReason(kRebootReason);
  watcher_manager().NotifyAll(/*watchdog=*/[] {});
  EXPECT_EQ(watcher_manager().NumWatchers(), kNumWatchers);

  loop()->RunUntilIdle();

  for (const auto& watcher : watchers) {
    ASSERT_TRUE(watcher.HasReason());
    EXPECT_EQ(watcher.Reason(), kRebootReason);

    EXPECT_EQ(watcher_manager().NumWatchers(), 0u);
  }
}

TEST_F(RebootWatcherManagerTestCase, Check_WatchdogExecutes) {
  std::vector<MockRebootMethodsWatcherDelaysReply> watchers;

  for (size_t i = 0; i < kNumWatchers; ++i) {
    watchers.emplace_back(dispatcher(), kWatchdogTimeout * 2);
  }

  for (auto& watcher : watchers) {
    zx::channel watcher_channel;
    zx::channel watcher_remote_channel;
    ASSERT_OK(zx::channel::create(0, &watcher_channel, &watcher_remote_channel));

    // The completer can be instantiated with a null transaction because the completer is unused
    // in |RebootWatcherManager|.
    watcher_manager().Register(
        std::move(watcher_channel),
        RebootMethodsWatcherRegister::Interface::RegisterCompleter::Sync(nullptr));
    fidl::Bind(dispatcher(), std::move(watcher_remote_channel), &watcher);
  }

  watcher_manager().SetRebootReason(kRebootReason);

  bool watchdog_executed = false;
  watcher_manager().NotifyAll(/*watchdog=*/[&] { watchdog_executed = true; });

  loop()->RunFor(kWatchdogTimeout);
  EXPECT_TRUE(watchdog_executed);

  // Let the watchers respond so their transactions are completed before the test is torn down.
  loop()->RunFor(kWatchdogTimeout);
}

TEST_F(RebootWatcherManagerTestCase, Check_ExecuteWatchdog) {
  std::vector<MockRebootMethodsWatcherDelaysReply> watchers;

  for (size_t i = 0; i < kNumWatchers; ++i) {
    watchers.emplace_back(dispatcher(), kWatchdogTimeout * 2);
  }

  for (auto& watcher : watchers) {
    zx::channel watcher_channel;
    zx::channel watcher_remote_channel;
    ASSERT_OK(zx::channel::create(0, &watcher_channel, &watcher_remote_channel));

    // The completer can be instantiated with a null transaction because the completer is unused
    // in |RebootWatcherManager|.
    watcher_manager().Register(
        std::move(watcher_channel),
        RebootMethodsWatcherRegister::Interface::RegisterCompleter::Sync(nullptr));
    fidl::Bind(dispatcher(), std::move(watcher_remote_channel), &watcher);
  }

  watcher_manager().SetRebootReason(kRebootReason);

  bool watchdog_executed = false;
  watcher_manager().NotifyAll(/*watchdog=*/[&] { watchdog_executed = true; });

  loop()->RunFor(kWatchdogTimeout / 2);

  watcher_manager().ExecuteWatchdog();

  loop()->RunUntilIdle();
  EXPECT_TRUE(watchdog_executed);

  // Let the watchers respond so their transactions are completed before the test is torn down.
  loop()->RunFor(kWatchdogTimeout + kWatchdogTimeout / 2);
}

TEST_F(RebootWatcherManagerTestCase, Check_OnLastReplyExecutes) {
  std::vector<MockRebootMethodsWatcher> watchers(kNumWatchers);

  for (auto& watcher : watchers) {
    zx::channel watcher_channel;
    zx::channel watcher_remote_channel;
    ASSERT_OK(zx::channel::create(0, &watcher_channel, &watcher_remote_channel));

    // The completer can be instantiated with a null transaction because the completer is unused
    // in |RebootWatcherManager|.
    watcher_manager().Register(
        std::move(watcher_channel),
        RebootMethodsWatcherRegister::Interface::RegisterCompleter::Sync(nullptr));
    fidl::Bind(dispatcher(), std::move(watcher_remote_channel), &watcher);
  }

  watcher_manager().SetRebootReason(kRebootReason);

  bool callback_executed = false;
  watcher_manager().NotifyAll(/*watchdog=*/[] {},
                              /*on_last_reply=*/[&] { callback_executed = true; });

  loop()->RunUntilIdle();

  EXPECT_TRUE(callback_executed);
}
