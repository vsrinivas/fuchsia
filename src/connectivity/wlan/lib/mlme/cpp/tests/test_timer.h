// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_TEST_TIMER_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_TEST_TIMER_H_

#include <lib/timekeeper/clock.h>
#include <lib/timekeeper/test_clock.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <wlan/mlme/timer.h>
#include <zircon/types.h>

namespace wlan {

struct TimerSchedulerImpl : public TimerScheduler {
    zx_status_t Schedule(Timer* timer, zx::time deadline) override { return ZX_OK; }

    zx_status_t Cancel(Timer* timer) override { return ZX_OK; }
};

class TestTimer final : public Timer {
   public:
    TestTimer(uint64_t id, timekeeper::TestClock* clock) : Timer(&scheduler_, id), clock_(clock) {}

    zx::time Now() const override { return clock_->Now(); }

   private:
    timekeeper::TestClock* clock_;
    TimerSchedulerImpl scheduler_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_TESTS_TEST_TIMER_H_
