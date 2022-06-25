// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/real_timer.h"

#include <lib/syslog/cpp/macros.h>

// System calls in this file should not fail unless the system is out-of-memory
// or we have a bug. Therefore we FX_CHECK that every system call succeeds.

namespace media_audio {

namespace {
constexpr auto kSignalForEvent = ZX_USER_SIGNAL_0;
constexpr auto kSignalForShutdown = ZX_USER_SIGNAL_1;
}  // namespace

// static
std::shared_ptr<RealTimer> RealTimer::Create(Config config) {
  struct MakePublicCtor : RealTimer {
    MakePublicCtor(Config config) : RealTimer(config) {}
  };
  return std::make_shared<MakePublicCtor>(config);
}

RealTimer::RealTimer(Config config) : slack_(config.timer_slack) {
  auto status = zx::timer::create(config.timer_slack_policy, ZX_CLOCK_MONOTONIC, &timer_);
  FX_CHECK(status == ZX_OK) << "Failed to create timer; status = " << status;
  FX_CHECK(slack_ >= zx::nsec(0)) << "Slack " << slack_.to_nsecs() << "ns < 0";
}

void RealTimer::SetEventBit() {
  auto status = timer_.signal(/* clear_mask = */ 0, /* set_mask = */ kSignalForEvent);
  FX_CHECK(status == ZX_OK) << "Failed to signal event bit; status = " << status;
}

void RealTimer::SetShutdownBit() {
  auto status = timer_.signal(/* clear_mask = */ 0, /* set_mask = */ kSignalForShutdown);
  FX_CHECK(status == ZX_OK) << "Failed to signal shutdown bit; status = " << status;
}

Timer::WakeReason RealTimer::SleepUntil(zx::time deadline) {
  FX_CHECK(!stopped_.load());

  constexpr auto kExpectedSignals = ZX_TIMER_SIGNALED | kSignalForEvent | kSignalForShutdown;

  // Reset the timer.
  auto status = timer_.cancel();
  FX_CHECK(status == ZX_OK) << "Failed to cancel timer; status = " << status;

  if (deadline < zx::time::infinite()) {
    status = timer_.set(deadline, slack_);
    FX_CHECK(status == ZX_OK) << "Failed to set timer; status = " << status;
  }

  // Wait for the next set of triggers.
  // This should not fail:
  // - We shouldn't get ZX_ERR_TIMED_OUT because the `wait_one` call has an infinite deadline
  // - We shouldn't get ZX_ERR_CANCELED unless there's a use-after-free bug on `this`
  zx_signals_t signals;
  status = timer_.wait_one(kExpectedSignals, zx::time::infinite(), &signals);
  FX_CHECK(status == ZX_OK) << "Failed to wait on timer; status = " << status;

  if ((signals & kSignalForEvent) != 0) {
    // The event bit is set. Before returning, we must clear the event bit on `timer_`.
    // Concurrent cases:
    //
    // * If SetEventBit is called between the above timer_.wait_one() and now,
    //   that call has no effect because the event bit is already set.
    //
    // * If SetEventBit is called between the following timer_.signal() and when
    //   this function returns, that call will effectively happen after this SleepUntil.
    //   The event bit will be read by the next SleepUntil call.
    //
    // Both of these cases are OK because SetEventBit has "at least once" semantics.
    // See comments in timer.h.
    auto status = timer_.signal(/* clear_mask = */ kSignalForEvent, /* set_mask = */ 0);
    FX_CHECK(status == ZX_OK) << "Failed to clear event bit; status = " << status;
  }

  return {
      .deadline_expired = (signals & ZX_TIMER_SIGNALED) != 0,
      .event_set = (signals & kSignalForEvent) != 0,
      .shutdown_set = (signals & kSignalForShutdown) != 0,
  };
}

void RealTimer::Stop() { stopped_.store(true); }

}  // namespace media_audio
