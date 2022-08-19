// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/utc_clock_ready_watcher.h"

#include <memory>

#include <gtest/gtest.h>

#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace {

constexpr timekeeper::time_utc kTime((zx::hour(7) + zx::min(14) + zx::sec(52)).get());

class UtcClockReadyWatcherTest : public UnitTestFixture {
 public:
  UtcClockReadyWatcherTest() {
    clock_.Set(kTime);

    zx_clock_create_args_v1_t clock_args{.backstop_time = 0};
    FX_CHECK(zx::clock::create(0u, &clock_args, &clock_handle_) == ZX_OK);

    utc_clock_ready_watcher_ = std::make_unique<UtcClockReadyWatcher>(
        dispatcher(), zx::unowned_clock(clock_handle_.get_handle()));
  }

 protected:
  void StartClock(const zx::time start_time = zx::time(kTime.get())) {
    if (const zx_status_t status =
            clock_handle_.update(zx::clock::update_args().set_value(start_time));
        status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "Failed to start clock";
    }
  }

  timekeeper::TestClock clock_;
  zx::clock clock_handle_;

  std::unique_ptr<UtcClockReadyWatcher> utc_clock_ready_watcher_;
};

TEST_F(UtcClockReadyWatcherTest, Check_ClockStarts) {
  bool clock_started{false};

  utc_clock_ready_watcher_->OnClockReady([&] { clock_started = true; });
  ASSERT_FALSE(clock_started);

  StartClock();
  RunLoopUntilIdle();

  ASSERT_TRUE(clock_started);
}

TEST_F(UtcClockReadyWatcherTest, Check_ClockStartedPreviously) {
  bool clock_started{false};
  StartClock();
  RunLoopUntilIdle();

  utc_clock_ready_watcher_->OnClockReady([&] { clock_started = true; });
  ASSERT_TRUE(clock_started);
}

TEST_F(UtcClockReadyWatcherTest, Check_ClockNeverStarts) {
  bool clock_started{false};

  utc_clock_ready_watcher_->OnClockReady([&] { clock_started = true; });
  ASSERT_FALSE(clock_started);

  for (size_t i = 0; i < 100; ++i) {
    RunLoopFor(zx::hour(23));
    EXPECT_FALSE(clock_started);
  }
}

}  // namespace
}  // namespace forensics
