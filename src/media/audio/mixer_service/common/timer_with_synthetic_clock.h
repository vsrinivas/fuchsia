// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_TIMER_WITH_SYNTHETIC_CLOCK_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_TIMER_WITH_SYNTHETIC_CLOCK_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/time.h>

#include <mutex>
#include <optional>

#include "src/media/audio/mixer_service/common/timer.h"

namespace media_audio {

// An implementation of Timer that uses a synthetic clock.
//
// Once a thread blocks in SleepUntil, it does not unblock until explicitly directed by
// a call to WakeAndAdvanceTo. This can be used by a controller thread to advance time
// deterministically in tests. A thread might control multiple timers using code like:
//
//   for (;;) {
//     for (auto& t : timers) {
//       t.WaitUntilSleeping();
//     }
//
//     // Check the timer status while all threads are sleeping.
//     zx::time next_deadline = zx::time::infinite();
//     bool has_event = false;
//     for (auto& t : timers) {
//       auto state = t.CurrentState();
//       next_deadline = std::min(state.deadline, next_deadline);
//       if (state.event_set) { has_event = true; }
//     }
//
//     // If there are no events pending, advance to the next deadline.
//     if (!has_event) {
//       now = std::max(now, next_deadline);
//     }
//     for (auto& t : timers) {
//       t.WakeAndAdvanceTo(now);
//     }
//  }
//
// This class is thread safe.
class TimerWithSyntheticClock : public Timer {
 public:
  explicit TimerWithSyntheticClock(zx::time start_time);

  // Implementation of Timer.
  void SetShutdownBit() override;
  void SetEventBit() override;
  WakeReason SleepUntil(zx::time deadline) override;

  // Blocks until a thread is blocked in SleepUntil.
  // May be called from any thread.
  void WaitUntilSleeping();

  // Wakes the currently-blocked SleepUntil after advancing to the given time.
  // May be called from any thread.
  // REQUIRES: currently sleeping and t >= now.
  void WakeAndAdvanceTo(zx::time t);

  struct State {
    std::optional<zx::time> deadline;  // std::nullopt if not sleeping
    bool event_set;                    // true if the "event" bit is set
    bool shutdown_set;                 // true if the "shutdown" bit is set
  };

  // Reports the current state of this timer.
  // May be called from any thread, however to ensure the state is not changing
  // concurrently, this should not be called unless all threads are blocked. See
  // example in the class comments.
  State CurrentState();

  // The current time.
  zx::time now();

 private:
  struct InternalState {
    explicit InternalState(zx::time start_time) : now(start_time) {}

    zx::time now;
    bool event_set = false;
    bool shutdown_set = false;

    // Notified when any of the following fields change.
    std::condition_variable cvar;
    std::optional<zx::time> deadline_if_sleeping;
    int64_t sleep_count = 0;
    int64_t advance_count = 0;
    int64_t wake_count = 0;
  };

  std::mutex mutex_;
  InternalState state_ TA_GUARDED(mutex_);
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_TIMER_WITH_SYNTHETIC_CLOCK_H_
