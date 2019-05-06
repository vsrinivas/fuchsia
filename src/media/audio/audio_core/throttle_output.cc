// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/throttle_output.h"

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_delta.h"
#include "src/media/audio/audio_core/audio_device_manager.h"

namespace media::audio {

static constexpr fxl::TimeDelta TRIM_PERIOD =
    fxl::TimeDelta::FromMilliseconds(10);

ThrottleOutput::ThrottleOutput(AudioDeviceManager* manager)
    : StandardOutputBase(manager) {}

ThrottleOutput::~ThrottleOutput() = default;

// static
fbl::RefPtr<AudioOutput> ThrottleOutput::Create(AudioDeviceManager* manager) {
  return fbl::AdoptRef<AudioOutput>(new ThrottleOutput(manager));
}

void ThrottleOutput::OnWakeup() {
  if (uninitialized_) {
    last_sched_time_ = fxl::TimePoint::Now();
    UpdatePlugState(true, 0);
    Process();
    uninitialized_ = false;
  }
}

bool ThrottleOutput::StartMixJob(MixJob* job, fxl::TimePoint process_start) {
  // Compute the next callback time; check whether trimming is falling behind.
  last_sched_time_ = last_sched_time_ + TRIM_PERIOD;
  if (process_start > last_sched_time_) {
    // TODO(mpuryear): Trimming is falling behind. We should tell someone.
    last_sched_time_ = process_start + TRIM_PERIOD;
  }

  // TODO(mpuryear): Optimize the trim operation by scheduling callbacks for
  // when our first pending packet ends, rather than polliing like this. This
  // will also tighten our timing in returning packets (currently, we hold
  // packets up to [TRIM_PERIOD-episilon] past their end PTS before releasing).
  //
  // To do this, we would need wake and recompute, whenever an AudioRenderer
  // client changes its rate transformation. For now, just polling is simpler.
  SetNextSchedTime(last_sched_time_);

  // Throttle outputs never actually mix anything. They provide backpressure to
  // the pipeline by holding AudioPacket references until their presentation is
  // finished. All we must do is schedule our next callback to keep things
  // running, and let the base class implementation handle trimming the output.
  return false;
}

bool ThrottleOutput::FinishMixJob(const MixJob& job) {
  // Since we never start any jobs, this should never be called.
  FXL_DCHECK(false);
  return false;
}

}  // namespace media::audio
