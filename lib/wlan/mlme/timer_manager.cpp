// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/timer_manager.h>

namespace wlan {

bool TimedEvent::IsActive() const {
    return deadline_ != kInactive;
}

bool TimedEvent::Triggered(zx::time now) const {
    if (!IsActive()) { return false; }

    return deadline_ <= now;
}

TimerManager::TimerManager(fbl::unique_ptr<Timer> timer) : timer_(std::move(timer)) {
    ZX_DEBUG_ASSERT(timer_ != nullptr);
}

TimerManager::~TimerManager() {
    if (timer_) { timer_->CancelTimer(); }
}

void TimerManager::CleanUp(zx::time now) {
    while (!events_.empty() && events_.top() <= now) {
        events_.pop();
    }
}

zx_status_t TimerManager::Schedule(zx::time deadline, TimedEvent* event) {
    ZX_DEBUG_ASSERT(timer_ != nullptr);
    ZX_DEBUG_ASSERT(event != nullptr);

    if (!events_.empty()) { CleanUp(Now()); };
    events_.push(deadline);

    if (events_.top() == deadline) {
        zx_status_t status = timer_->SetTimer(deadline);
        if (status != ZX_OK) { return status; }
    }

    *event = TimedEvent(deadline);
    return ZX_OK;
}

zx::time TimerManager::HandleTimeout() {
    ZX_DEBUG_ASSERT(timer_ != nullptr);

    zx::time now = Now();
    CleanUp(now);

    timer_->CancelTimer();
    if (!events_.empty()) { timer_->SetTimer(events_.top()); }

    return now;
}

}  // namespace wlan
