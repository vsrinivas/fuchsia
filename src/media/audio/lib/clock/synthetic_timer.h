// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_SYNTHETIC_TIMER_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_SYNTHETIC_TIMER_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/time.h>

#include <memory>
#include <mutex>
#include <optional>

#include "src/media/audio/lib/clock/timer.h"

namespace media_audio {

// An implementation of Timer that is controlled by a SyntheticClockRealm. Once a thread blocks in
// SleepUntil(T), it does not unblock until receiving a call to AdvanceTo(T') where `T' >= T`.
//
// This class is thread safe.
class SyntheticTimer : public Timer {
 public:
  [[nodiscard]] static std::shared_ptr<SyntheticTimer> Create(zx::time mono_start_time);

  // Implementation of Timer.
  void SetShutdownBit() override;
  void SetEventBit() override;
  WakeReason SleepUntil(zx::time deadline) override;
  void Stop() override;

  // Blocks until stopped or until a thread is blocked in SleepUntil. This is intended to be used
  // from SyntheticClockRealm using code like:
  //
  // ```
  // void AdvanceTo(when) {
  //   while (now_ < when) {
  //     for (auto& t : timers) {
  //       t.WaitUntilSleepingOrStopped();
  //     }
  //
  //     // Use SyntheticTimer::CurrentState to compute the next deadline and check
  //     // if any events are pending, then advance to the next deadline.
  //   }
  // }
  // ```
  //
  // May be called from any thread.
  void WaitUntilSleepingOrStopped();

  // Advances to the given system monotonic time. If a thread is currently blocked in
  // `SleepUntil(deadline)` with `deadline <= t`, the blocked thread is woken. If called with `t <
  // deadline`, the blocked thread will be woken iff there is a pending signal.
  //
  // May be called from any thread.
  //
  // REQUIRES: currently sleeping and t >= now.
  void AdvanceTo(zx::time t);

  struct State {
    std::optional<zx::time> deadline;  // std::nullopt if not sleeping
    bool event_set;                    // true if the "event" bit is set
    bool shutdown_set;                 // true if the "shutdown" bit is set
    bool stopped;                      // true if stopped
  };

  // Reports the current state of this timer.
  // May be called from any thread, however to ensure the state is not changing
  // concurrently, this should not be called unless all threads are blocked. See
  // example in the class comments.
  State CurrentState();

  // The current system monotonic time.
  zx::time now();

 private:
  explicit SyntheticTimer(zx::time mono_start_time);

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
    bool stopped = false;
  };

  std::mutex mutex_;
  InternalState state_ TA_GUARDED(mutex_);
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_SYNTHETIC_TIMER_H_
