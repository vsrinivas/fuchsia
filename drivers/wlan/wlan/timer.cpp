// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "timer.h"

#include <utility>

namespace wlan {

Timer::Timer(uint64_t id) : id_(id) {}

Timer::~Timer() {}

zx_status_t Timer::SetTimer(zx_time_t deadline) {
    deadline_ = deadline;
    return SetTimerImpl(deadline);
}

zx_status_t Timer::CancelTimer() {
    deadline_ = 0u;
    return CancelTimerImpl();
}

SystemTimer::SystemTimer(uint64_t id, zx::timer timer)
  : Timer(id), timer_(std::move(timer)) {}

zx_status_t SystemTimer::SetTimerImpl(zx_time_t deadline) {
    return timer_.set(deadline, 0);
}

zx_status_t SystemTimer::CancelTimerImpl() {
    return timer_.cancel();
}

zx_status_t TestTimer::SetTimerImpl(zx_time_t duration) {
    return ZX_OK;
}

zx_status_t TestTimer::CancelTimerImpl() {
    return ZX_OK;
}

}  // namespace wlan
