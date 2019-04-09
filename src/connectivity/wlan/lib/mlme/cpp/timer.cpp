// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/timer.h>

#include <zircon/system/public/zircon/assert.h>
#include <utility>

namespace wlan {

Timer::Timer(uint64_t id) : id_(id) {}

Timer::~Timer() {}

zx_status_t Timer::SetTimer(zx::time deadline) {
    deadline_ = deadline;
    return SetTimerImpl(deadline);
}

zx_status_t Timer::CancelTimer() {
    deadline_ = zx::time();
    return CancelTimerImpl();
}

SystemTimer::SystemTimer(uint64_t id, zx::timer timer) : Timer(id), timer_(std::move(timer)) {}

zx_status_t SystemTimer::SetTimerImpl(zx::time deadline) {
    if (!timer_) { return ZX_ERR_BAD_STATE; }
    return timer_.set(deadline, zx::nsec(0));
}

zx_status_t SystemTimer::CancelTimerImpl() {
    if (!timer_) { return ZX_ERR_BAD_STATE; }
    return timer_.cancel();
}

}  // namespace wlan
