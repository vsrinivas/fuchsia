// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/synthetic_timer.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/lib/clock/scoped_unique_lock.h"

namespace media_audio {

// static
std::shared_ptr<SyntheticTimer> SyntheticTimer::Create(zx::time mono_start_time) {
  struct MakePublicCtor : SyntheticTimer {
    MakePublicCtor(zx::time mono_start_time) : SyntheticTimer(mono_start_time) {}
  };
  return std::make_shared<MakePublicCtor>(mono_start_time);
}

SyntheticTimer::SyntheticTimer(zx::time mono_start_time) : state_(mono_start_time) {}

void SyntheticTimer::SetEventBit() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.event_set = true;
}

void SyntheticTimer::SetShutdownBit() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.shutdown_set = true;
}

Timer::WakeReason SyntheticTimer::SleepUntil(zx::time deadline) {
  scoped_unique_lock<std::mutex> lock(mutex_);
  FX_CHECK(!state_.stopped);

  WakeReason wake_reason;
  do {
    // Notify WaitUntilSleepingOrStopped that we are sleeping, then wait for AdvanceTo.
    state_.deadline_if_sleeping = deadline;
    state_.sleep_count++;
    state_.cvar.notify_all();
    state_.cvar.wait(
        lock, [this]() TA_REQ(mutex_) { return state_.advance_count == state_.sleep_count; });

    wake_reason = {
        .deadline_expired = state_.now >= deadline,
        .event_set = state_.event_set,
        .shutdown_set = state_.shutdown_set,
    };

    // Try again if there's no reason to wake.
  } while (!wake_reason.deadline_expired && !wake_reason.event_set && !wake_reason.shutdown_set);

  // No longer sleeping.
  state_.deadline_if_sleeping = std::nullopt;
  state_.wake_count++;
  state_.cvar.notify_all();

  // Need to clear before returning: see timer.h.
  state_.event_set = false;

  return wake_reason;
}

void SyntheticTimer::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.stopped = true;
  state_.cvar.notify_all();
}

void SyntheticTimer::WaitUntilSleepingOrStopped() {
  scoped_unique_lock<std::mutex> lock(mutex_);
  state_.cvar.wait(
      lock, [this]() TA_REQ(mutex_) { return state_.deadline_if_sleeping || state_.stopped; });
}

void SyntheticTimer::AdvanceTo(zx::time t) {
  scoped_unique_lock<std::mutex> lock(mutex_);

  FX_CHECK(state_.deadline_if_sleeping || state_.stopped)
      << "Must be called while the Timer is sleeping or stopped";
  FX_CHECK(t >= state_.now) << "Cannot go backwards from " << state_.now.get() << " to " << t.get();

  // Advance the current time.
  state_.now = t;

  // Don't wake SleepUntil unless there is a pending signal or the deadline has expired.
  if (state_.stopped ||
      (t < *state_.deadline_if_sleeping && !state_.event_set && !state_.shutdown_set)) {
    return;
  }

  state_.advance_count++;
  state_.cvar.notify_all();

  // Wait until SleepUntil returns so that commands which happen-after this function
  // call won't be observed by the sleeper. For example, given a sequence:
  //
  //   timer.AdvanceTo(x)   ---- wakes ---->   timer.SleepUntil
  //   timer.SetEventBit()
  //
  // Assuming the timer's event bit is not initially set, the SleepUntil call should
  // not report `event_set = true`.
  state_.cvar.wait(lock,
                   [this]() TA_REQ(mutex_) { return state_.wake_count == state_.advance_count; });
}

SyntheticTimer::State SyntheticTimer::CurrentState() {
  std::lock_guard<std::mutex> lock(mutex_);
  return {
      .deadline = state_.deadline_if_sleeping,
      .event_set = state_.event_set,
      .shutdown_set = state_.shutdown_set,
      .stopped = state_.stopped,
  };
}

zx::time SyntheticTimer::now() {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.now;
}

}  // namespace media_audio
