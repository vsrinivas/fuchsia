// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_TIMER_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_TIMER_H_

#include <lib/zx/time.h>
#include <zircon/types.h>

#include <functional>

namespace media_audio {

// A Timer is built around one core operation, `SleepUntil`, which puts a thread to sleep until a
// deadline is reached. Threads can be interrupted by two signals: a "shutdown" bit which signals
// that the thread should exit, and an "event" bit which signals that new work has arrived.
// Typically this is used in a loop:
//
// ```
// for (;;) {
//   auto wake_reason = timer_->SleepUntil(DeadlineForNextScheduledJob());
//   if (wake_reason.shutdown_set) {
//     timer->Stop();
//     return;
//   }
//   if (wake_reason.event_set) {
//     // check for new work
//   }
//   if (wake_reason.deadline_expired) {
//     // do next scheduled job
//   }
// }
// ```
//
// This is an abstract class so we can provide implementations that use real and synthetic clocks.
class Timer {
 public:
  Timer() = default;
  virtual ~Timer() = default;
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;
  Timer(Timer&&) = delete;
  Timer& operator=(Timer&&) = delete;

  // Interrupts the timer by setting a generic "event" bit. If a thread is blocked in SleepUntil,
  // that thread is woken immediately. Otherwise the next SleepUntil call will return immediately.
  // Implementations must be safe to call from any thread.
  virtual void SetEventBit() = 0;

  // Interrupts the timer by setting a "shutdown" bit. If a thread is blocked in SleepUntil, that
  // thread is woken immediately. Otherwise the next SleepUntil call will return immediately.
  // Implementations must be safe to call from any thread.
  virtual void SetShutdownBit() = 0;

  // Reason that SleepUntil returned.
  struct WakeReason {
    bool deadline_expired;  // woke because the deadline was reached
    bool event_set;         // woke because the "event" bit was set
    bool shutdown_set;      // woke because the "shutdown" bit was set
  };

  // Sleeps until the given `deadline`, relative to the system monotonic clock, or until interrupted
  // by SetShutdownBit or SetEventBit. Returns the reason for waking.
  //
  // Just before returning, SleepUntil atomically clears the event bit. This gives the SetEventBit
  // method "at least once" semantics: after SetEventBit is called, at least one future SleepUntil
  // call must return with `event_set = true`. If SetEventBit happens concurrently with SleepUntil,
  // it is unspecified whether the concurrent SleepUntil will recognize the event.
  //
  // The shutdown bit is not cleared: once set, it persists indefinitely.
  //
  // Implementations must be safe to call from any thread, however it must be called by at most one
  // thread at a time.
  virtual WakeReason SleepUntil(zx::time deadline) = 0;

  // Declares that there will not be any further calls to SleepUntil. Implementations must be safe
  // to call from any thread, however in practice this is normally called by the thread which loops
  // on SleepUntil, as illustrated in the class comments.
  virtual void Stop() = 0;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_TIMER_H_
