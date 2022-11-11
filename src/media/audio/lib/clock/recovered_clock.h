// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_RECOVERED_CLOCK_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_RECOVERED_CLOCK_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/clock.h>

#include <memory>
#include <optional>
#include <string>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/pid_control.h"

namespace media_audio {

// A wrapper that allows "recovering" a clock from a stream of position updates. Each position
// update has the form `(mono_time, position)`, where `mono_time` is the system monotonic time
// at which the `position` was observed. The `position` is any 64-bit integer, such as an index
// into a byte stream, where the `position` must advance at a constant rate relative to the
// reference clock we are recovering.
//
// Given a position update, combined with a function `ref_time(p)` that translates from position
// to the expected reference time at that position, we can compute a clock error:
//
// ```
// mono_time - clock->MonotonicTimeFromReferenceTime(ref_time(position))
// ```
//
// If this clock error is non-zero, the RecoveredClock is adjusted to (attempt to) eliminate that
// error in future position readings. For example, a RecoveredClock can approximate a hardware
// device clock that can be read only indirectly via position updates from a device driver.
//
// All methods are safe to call from any thread.
class RecoveredClock : public Clock {
 public:
  // TODO(fxbug.dev/114922): move PidControl to media_audio.
  using PidControl = media::audio::clock::PidControl;

  // Creates a RecoveredClock which drives the given `backing_clock`, which must be adjustable.
  // The backing clock is adjusted using a PID controller with the given coefficients.
  static std::shared_ptr<RecoveredClock> Create(std::string_view name,
                                                std::shared_ptr<Clock> backing_clock,
                                                PidControl::Coefficients pid_coefficients);

  std::string_view name() const override { return name_; }
  zx_koid_t koid() const override { return backing_clock_->koid(); }
  uint32_t domain() const override { return backing_clock_->domain(); }
  bool adjustable() const override { return false; }

  zx::time now() const override { return backing_clock_->now(); }
  ToClockMonoSnapshot to_clock_mono_snapshot() const override {
    return backing_clock_->to_clock_mono_snapshot();
  }

  // Although a RecoveredClock's rate can change over time, the clock cannot be adjusted directly.
  // All adjustments happen via `Reset` and `Update`.
  void SetRate(int32_t rate_adjust_ppm) override {
    FX_CHECK(false) << "RecoveredClocks are not adjustable";
  }

  std::optional<zx::clock> DuplicateZxClockReadOnly() const override {
    return backing_clock_->DuplicateZxClockReadOnly();
  }

  // Resets the clock's rate to match the system monotonic clock, clears all accumulated state,
  // and starts using a new translation from position to reference time.
  void Reset(zx::time mono_reset_time, media::TimelineFunction pos_to_ref_time);

  // Tunes the clock based on an updated position. Returns the clock's predicted monotonic time.
  // There must be at least one Reset before the first Update. The sequence of Reset and
  // Update calls must use monotonically-increasing values for both time and position.
  zx::time Update(zx::time mono_time, int64_t position);

 private:
  RecoveredClock(std::string_view name, std::shared_ptr<Clock> backing_clock,
                 PidControl::Coefficients pid_coefficients)
      : name_(name), backing_clock_(std::move(backing_clock)), pid_(pid_coefficients) {}

  void SetBackingRate(int32_t rate_adjust_ppm) TA_REQ(mutex_);

  const std::string name_;
  const std::shared_ptr<Clock> backing_clock_;

  std::mutex mutex_;
  int32_t current_backing_rate_ppm_ TA_GUARDED(mutex_) = 0;
  std::optional<media::TimelineFunction> pos_to_ref_time_ TA_GUARDED(mutex_);
  PidControl pid_ TA_GUARDED(mutex_);
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_RECOVERED_CLOCK_H_
