// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_TIMER_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_TIMER_H_

#include <lib/timekeeper/system_clock.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <zircon/types.h>

namespace wlan {

class Timer;

struct TimerScheduler {
    virtual zx_status_t Schedule(Timer* timer, zx::time deadline) = 0;
    virtual zx_status_t Cancel(Timer* timer) = 0;
};

class Timer {
   public:
    explicit Timer(TimerScheduler* scheduler, uint64_t id);
    virtual ~Timer();

    virtual zx::time Now() const = 0;

    // TODO(tkilbourn): add slack
    zx_status_t SetTimer(zx::time deadline);
    zx_status_t CancelTimer();

    uint64_t id() const { return id_; }
    zx::time deadline() const { return deadline_; }

   private:
    TimerScheduler* scheduler_;
    uint64_t id_;
    zx::time deadline_;
};

class SystemTimer final : public Timer {
   public:
    SystemTimer(TimerScheduler* scheduler, uint64_t id, zx::timer timer);

    zx::time Now() const override { return clock_.Now(); }
    zx::timer* inner() { return &timer_; }

   private:
    timekeeper::SystemClock clock_;
    zx::timer timer_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_TIMER_H_
