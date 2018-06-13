// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/throttle_output.h"

#include "garnet/bin/media/audio_server/audio_device_manager.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

namespace media {
namespace audio {

static constexpr fxl::TimeDelta TRIM_PERIOD =
    fxl::TimeDelta::FromMilliseconds(10);

ThrottleOutput::ThrottleOutput(AudioDeviceManager* manager)
    : StandardOutputBase(manager) {}

ThrottleOutput::~ThrottleOutput() {}

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
  // Compute our next callback time, and check to see if we are falling behind
  // in the process.
  last_sched_time_ = last_sched_time_ + TRIM_PERIOD;
  if (process_start > last_sched_time_) {
    // TODO(johngro): We are falling behind on our trimming.  We should
    // probably tell someone.
    last_sched_time_ = process_start + TRIM_PERIOD;
  }

  // TODO(johngro): We could optimize this trim operation by scheduling our
  // callback to the time at which the first pending packet in our queue will
  // end, instead of using this polling style.  This would have the addition
  // benefit of tighten the timing on returning packets (currently, we could
  // hold a packet for up to TRIM_PERIOD - episilon past its end pts before
  // releasing it)
  //
  // In order to do this, however, we would to wake up and recompute whenever
  // the rate transformations for one of our client renderers changes.  For now,
  // we just poll because its simpler.
  SetNextSchedTime(last_sched_time_);

  // The throttle output never actually mixes anything, it just provides
  // backpressure to the pipeline by holding references to AudioPackets until
  // after their presentation should be finished.  All we need to do here is
  // schedule our next callback to keep things running, and let the base class
  // implementation handle trimming the output.
  return false;
}

bool ThrottleOutput::FinishMixJob(const MixJob& job) {
  // Since we never start any jobs, this should never be called.
  FXL_DCHECK(false);
  return false;
}

}  // namespace audio
}  // namespace media
