// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_UNADJUSTABLE_CLOCK_WRAPPER_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_UNADJUSTABLE_CLOCK_WRAPPER_H_

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/lib/clock/clock.h"

namespace media_audio {

// Wraps a backing clock, always reports "unadjustable" whether or not the backing clock is
// adjustable. This gives an unadjustable view of any adjustable clock.
//
// All methods are safe to call from any thread.
class UnadjustableClockWrapper : public Clock {
 public:
  explicit UnadjustableClockWrapper(std::shared_ptr<Clock> backing_clock)
      : backing_clock_(std::move(backing_clock)) {}

  std::string_view name() const override { return backing_clock_->name(); }
  zx_koid_t koid() const override { return backing_clock_->koid(); }
  uint32_t domain() const override { return backing_clock_->domain(); }
  bool adjustable() const override { return false; }

  zx::time now() const override { return backing_clock_->now(); }

  ToClockMonoSnapshot to_clock_mono_snapshot() const override {
    return backing_clock_->to_clock_mono_snapshot();
  }

  void SetRate(int32_t rate_adjust_ppm) override {
    FX_CHECK(false) << "UnadjustableClockWrapper is not adjustable, clock is " << name();
  }

  std::optional<zx::clock> DuplicateZxClockReadOnly() const override {
    return backing_clock_->DuplicateZxClockReadOnly();
  }

 private:
  const std::shared_ptr<Clock> backing_clock_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_UNADJUSTABLE_CLOCK_WRAPPER_H_
