// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <wlan/mlme/timer.h>

namespace wlan {

Timer::Timer(TimerScheduler* scheduler, uint64_t id) : scheduler_(scheduler), id_(id) {}

Timer::~Timer() {}

zx_status_t Timer::SetTimer(zx::time deadline) {
  deadline_ = deadline;
  return scheduler_->Schedule(this, deadline);
}

zx_status_t Timer::CancelTimer() {
  deadline_ = zx::time();
  return scheduler_->Cancel(this);
}

SystemTimer::SystemTimer(TimerScheduler* scheduler, uint64_t id, zx::timer timer)
    : Timer(scheduler, id), timer_(std::move(timer)) {}

}  // namespace wlan
