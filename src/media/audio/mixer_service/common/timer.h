// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_TIMER_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_TIMER_H_

#include <lib/zx/time.h>
#include <zircon/types.h>

#include <functional>

namespace media_audio {

// An abstract wrapper around a zx::timer. The class can sleep until a timer
// fires or until a signal bit is set. This is an abstract class so we can
// provide implementations that use real and synthetic clocks.
class Timer {
 public:
  Timer() = default;
  virtual ~Timer() = default;
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;
  Timer(Timer&&) = delete;
  Timer& operator=(Timer&&) = delete;

  // Sets a generic "event" bit.
  // Implementations must be safe to call from any thread.
  virtual void SetEventBit() = 0;

  // Sets a "shutdown" bit.
  // Implementations must be safe to call from any thread.
  virtual void SetShutdownBit() = 0;

  // Reason that SleepUntil returned.
  struct WakeReason {
    bool deadline_expired;  // woke because the deadline was reached
    bool event_set;         // woke because the "event" bit was set
    bool shutdown_set;      // woke because the "shutdown" bit was set
  };

  // Sleeps until the given `deadline` or until interrupted by SetShutdownBit
  // or SetEventBit. Returns the reason for waking.
  //
  // Just before returning, SleepUntil atomically clears the event bit. This gives
  // the SetEventBit method "at least once" semantics: after SetEventBit is called,
  // at least one future SleepUntil call must return with `event_set = true`. If
  // SetEventBit happens concurrently with SleepUntil, it is unspecified whether the
  // concurrent SleepUntil will recognize the event.
  //
  // The shutdown bit is not cleared: once set, it persists indefinitely.
  //
  // Implementations must be safe to call from any thread, however it must be called
  // by at most one thread at a time.
  virtual WakeReason SleepUntil(zx::time deadline) = 0;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_TIMER_H_
