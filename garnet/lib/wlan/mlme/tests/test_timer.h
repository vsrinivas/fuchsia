// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_TESTS_TEST_TIMER_H_
#define GARNET_LIB_WLAN_MLME_TESTS_TEST_TIMER_H_

#include <lib/timekeeper/clock.h>
#include <lib/timekeeper/test_clock.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <wlan/mlme/timer.h>
#include <zircon/types.h>

namespace wlan {

class TestTimer final : public Timer {
   public:
    TestTimer(uint64_t id, timekeeper::TestClock* clock) : Timer(id), clock_(clock) {}

    zx::time Now() const override { return clock_->Now(); }

   protected:
    zx_status_t SetTimerImpl(zx::time deadline) override;
    zx_status_t CancelTimerImpl() override;

   private:
    timekeeper::TestClock* clock_;
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_TESTS_TEST_TIMER_H_
