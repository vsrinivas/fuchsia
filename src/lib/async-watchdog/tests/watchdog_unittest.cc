// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/async-watchdog/watchdog.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace async_watchdog {

using WatchdogUnittest = gtest::TestLoopFixture;

class TestWatchdog {
 public:
  TestWatchdog(uint64_t warning_interval_ms, uint64_t timeout_ms,
               async::LoopInterface* watchdog_loop, async::LoopInterface* watched_thread_loop,
               fit::closure run_update, fit::function<bool(void)> check_update)
      : watchdog_loop_(watchdog_loop), watched_thread_loop_(watched_thread_loop) {
    watchdog_impl_ = std::make_unique<WatchdogImpl>(
        "TestWatchdog thread", warning_interval_ms, timeout_ms, watchdog_loop_->dispatcher(),
        watched_thread_loop_->dispatcher(), std::move(run_update), std::move(check_update));
    watchdog_impl_->Initialize();
  }
  ~TestWatchdog() { watchdog_impl_->Finalize(); }

 private:
  async::LoopInterface* watchdog_loop_;
  async::LoopInterface* watched_thread_loop_;
  std::unique_ptr<WatchdogImpl> watchdog_impl_;
};

// This tests whether the watchdog can run for every
// |kWatchdogTimeoutMs| ms.
TEST_F(WatchdogUnittest, Basic) {
  const uint64_t kWatchdogTimeoutMs = 12ul;
  std::shared_ptr<int> counter_update = std::make_shared<int>(0);
  std::shared_ptr<int> counter_check = std::make_shared<int>(0);
  auto watchdog_loop = test_loop().StartNewLoop();
  auto watched_thread_loop = test_loop().StartNewLoop();
  TestWatchdog watchdog(
      kWatchdogTimeoutMs, kWatchdogTimeoutMs, watchdog_loop.get(), watched_thread_loop.get(),
      [counter_update]() { (*counter_update)++; },
      [counter_check]() {
        (*counter_check)++;
        return true;
      });
  EXPECT_EQ(*counter_update, 0);
  EXPECT_EQ(*counter_check, 0);
  RunLoopFor(zx::msec(25));
  // Update at 3s, 6s, 9s, 15s, 18s, 21s.
  EXPECT_EQ(*counter_update, 6);
  // Check at 12s, 24s.
  EXPECT_EQ(*counter_check, 2);
}

// This tests whether the watchdog can detect the failure and end the
// process if Scenic timeouts (here we do not run the test main loop
// to simulate the situation where Scenic is not responsive).
TEST_F(WatchdogUnittest, TimeoutTest) {
  const uint64_t kWatchdogTimeoutMs = 5ul;
  auto unexecuted_loop = async::TestLoop();
  auto watchdog_loop = test_loop().StartNewLoop();
  auto watched_thread_loop = unexecuted_loop.StartNewLoop();
  std::shared_ptr<bool> triggered = std::make_shared<bool>(false);
  EXPECT_DEATH(
      {
        TestWatchdog watchdog(
            kWatchdogTimeoutMs, kWatchdogTimeoutMs, watchdog_loop.get(), watched_thread_loop.get(),
            [triggered]() { *triggered = true; }, [triggered]() { return *triggered; });
        RunLoopFor(zx::msec(20));
      },
      "");
}

// This checks whether it works correctly if we use different values for
// |warning_interval| and |timeout| in WatchdogImpl.
TEST_F(WatchdogUnittest, DISABLED_MultipleTimeoutsAllowed) {
  const uint64_t kWatchdogWarningInterval = 5ul;
  const uint64_t kWatchdogTimeoutMs = 15ul;
  std::shared_ptr<int> counter_check = std::make_shared<int>(0);
  auto watchdog_loop = test_loop().StartNewLoop();
  auto watched_thread_loop = test_loop().StartNewLoop();
  FX_LOGS(INFO) << "Scenic errors are expected in this test case.";
  TestWatchdog watchdog(
      kWatchdogWarningInterval, kWatchdogTimeoutMs, watchdog_loop.get(), watched_thread_loop.get(),
      []() {},
      [counter_check]() {
        (*counter_check)++;
        return false;
      });
  EXPECT_EQ(*counter_check, 0);
  RunLoopFor(zx::msec(14));
  // Check at 5s, 10s and the watchdog should not crash.
  EXPECT_EQ(*counter_check, 2);
}

}  // namespace async_watchdog
