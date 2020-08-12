// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "watchdog.h"

#include "macros.h"

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

  // This needs to be reasonably low to let vp9_decoder_fuzzer_test do enough iterations fast enough
  // to avoid tests timing out, as that test wedges the VP9 HW decoder in some iterations.  Also,
  // for now, when the watchdog fires for one stream, any other stream being decoded concurrently
  // will be adversely impacted.  To fix that we'd need to more directly tell that the HW is stuck
  // decoding, so the stream with bad data can get out of the way faster when HW is stuck decoding
  // the bad stream.
  //
  // TODO(fxbug.dev/49526): Have the watchdog wake up sooner and more often, and have it check on
  // the stream buffer read pointer progress.  If that progress stops for even a fairly short time,
  // we can fire the watchdog fairly quickly.  And/or work toward changing the FW for the VP9 HW
  // decoder to generate an interrupt on bad input data instead of getting wedged.  And/or when
  // watchdog fires read HevcAssistMbox0IrqReg to see if an interrupt is already pending that
  // HandleInterrupt doesn't know about yet (TBD whether reading that register works and is
  // meaningful).
  constexpr uint32_t kWatchdogTimeoutMs = 4000;
  timeout_time_ = zx::deadline_after(zx::msec(kWatchdogTimeoutMs));

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
