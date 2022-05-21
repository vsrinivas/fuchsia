// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/recovered_clock.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <cmath>
#include <string>

#include "src/media/audio/lib/clock/logging.h"

namespace media_audio {

// static
std::shared_ptr<RecoveredClock> RecoveredClock::Create(std::string_view name,
                                                       std::shared_ptr<Clock> backing_clock,
                                                       PidControl::Coefficients pid_coefficients) {
  // The clock must be adjustable.
  FX_CHECK(backing_clock->adjustable());
  FX_CHECK(backing_clock->domain() != kMonotonicDomain);

  struct MakePublicCtor : RecoveredClock {
    MakePublicCtor(std::string_view name, std::shared_ptr<Clock> backing_clock,
                   PidControl::Coefficients pid_coefficients)
        : RecoveredClock(name, std::move(backing_clock), pid_coefficients) {}
  };

  return std::make_shared<MakePublicCtor>(name, std::move(backing_clock), pid_coefficients);
}

void RecoveredClock::Reset(zx::time mono_reset_time, media::TimelineFunction pos_to_ref_time) {
  std::lock_guard<std::mutex> lock(mutex_);
  pid_.Start(mono_reset_time);
  pos_to_ref_time_ = pos_to_ref_time;
  SetBackingRate(0);
}

zx::time RecoveredClock::Update(zx::time mono_time, int64_t position) {
  std::lock_guard<std::mutex> lock(mutex_);
  FX_CHECK(pos_to_ref_time_.has_value()) << "must call Reset before Update";

  auto ref_time = zx::time(pos_to_ref_time_->Apply(position));
  auto predicted_mono_time = backing_clock_->MonotonicTimeFromReferenceTime(ref_time);
  auto error = predicted_mono_time - mono_time;

  pid_.TuneForError(mono_time, static_cast<double>(error.to_nsecs()));
  int32_t rate_adjust_ppm = ClampDoubleToZxClockPpm(pid_.Read());
  LogClockAdjustment(*this, current_backing_rate_ppm_, rate_adjust_ppm, error, pid_);
  SetBackingRate(rate_adjust_ppm);

  return predicted_mono_time;
}

void RecoveredClock::SetBackingRate(int32_t rate_adjust_ppm) {
  if (current_backing_rate_ppm_ != rate_adjust_ppm) {
    backing_clock_->SetRate(rate_adjust_ppm);
    current_backing_rate_ppm_ = rate_adjust_ppm;
  }
}

}  // namespace media_audio
