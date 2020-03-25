// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "watchdog.h"

Watchdog::Watchdog(Owner* owner) : owner_(owner), loop_(&kAsyncLoopConfigNeverAttachToThread) {
  ZX_ASSERT(ZX_OK == loop_.StartThread("Watchdog"));
  // Use late slack so the comparison in CheckAndResetTimeout works.
  zx::timer::create(ZX_TIMER_SLACK_LATE, ZX_CLOCK_MONOTONIC, &timer_);
  waiter_.set_object(timer_.get());
  waiter_.set_trigger(ZX_TIMER_SIGNALED);
  waiter_.Begin(loop_.dispatcher());
}

void Watchdog::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  ZX_ASSERT(!timer_running_);
  // TODO(48599): Change this timeout back to 200ms (or at least lower than 15 seconds) after
  // Vp9Decoder::OnSignaledWatchdog() is able to successfully reset.  As a rule of thumb, unless we do something to
  // get higher priority / higher scheduling policy for interrupt handling thread, this duration should be on the order
  // of 1/2 the typical interval between keyframe(s), as we're making a tradeoff between recovering by waiting and
  // recovering by resetting, and streams shouldn't be corrupted, so we should give plenty of time for waiting to
  // work, as that is likely to skip fewer frames overall.
  constexpr uint32_t kTimeoutMs = 15000;
  timeout_time_ = zx::deadline_after(zx::msec(kTimeoutMs));

  timer_.set(timeout_time_, zx::duration());
  timer_running_ = true;
}

void Watchdog::Cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  timer_running_ = false;
  timer_.cancel();
}

// Returns true if the watchdog is timed out, and also resets the watchdog if that happened.
bool Watchdog::CheckAndResetTimeout() {
  std::lock_guard<std::mutex> lock(mutex_);
  // It's possible the timer timed out but was cancelled and restarted between the wait being
  // signaled and this call, so check if the current time is greater than the expected timeout.
  if (timer_running_ && zx::clock::get_monotonic() >= timeout_time_) {
    timer_.cancel();
    timer_running_ = false;
    return true;
  }
  return false;
}

bool Watchdog::is_running() {
  std::lock_guard<std::mutex> lock(mutex_);
  return timer_running_;
}

void Watchdog::HandleTimer(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                           zx_status_t status, const zx_packet_signal_t* signal) {
  if (status == ZX_ERR_CANCELED)
    return;
  // Don't hold mutex_ around OnSignaledWatchdog call, because that would cause lock ordering
  // issues.
  owner_->OnSignaledWatchdog();
  waiter_.Begin(loop_.dispatcher());
}
