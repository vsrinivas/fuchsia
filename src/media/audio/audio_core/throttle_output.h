// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_THROTTLE_OUTPUT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_THROTTLE_OUTPUT_H_

#include <fuchsia/media/cpp/fidl.h>

#include <fbl/ref_ptr.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/fxl/time/time_delta.h"
#include "src/media/audio/audio_core/audio_output.h"

namespace media::audio {

static constexpr fxl::TimeDelta TRIM_PERIOD = fxl::TimeDelta::FromMilliseconds(10);

class ThrottleOutput : public AudioOutput {
 public:
  static fbl::RefPtr<AudioOutput> Create(AudioDeviceManager* manager) {
    return fbl::AdoptRef<AudioOutput>(new ThrottleOutput(manager));
  }

  ~ThrottleOutput() override = default;

 protected:
  explicit ThrottleOutput(AudioDeviceManager* manager) : AudioOutput(manager) {}

  // AudioOutput Implementation
  void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) override {
    if (uninitialized_) {
      last_sched_time_ = fxl::TimePoint::Now();
      UpdatePlugState(true, 0);
      Process();
      uninitialized_ = false;
    }
  }

  bool StartMixJob(MixJob* job, fxl::TimePoint process_start) override {
    // Compute the next callback time; check whether trimming is falling behind.
    last_sched_time_ = last_sched_time_ + TRIM_PERIOD;
    if (process_start > last_sched_time_) {
      // TODO(mpuryear): Trimming is falling behind. We should tell someone.
      last_sched_time_ = process_start + TRIM_PERIOD;
    }

    // TODO(mpuryear): Optimize the trim operation by scheduling callbacks for
    // when our first pending packet ends, rather than polling . This will also
    // tighten our timing in returning packets (currently, we hold packets up to
    // [TRIM_PERIOD-episilon] past their end PTS before releasing).
    //
    // To do this, we would need wake and recompute, whenever an AudioRenderer
    // client changes its rate transformation. For now, just polling is simpler.
    SetNextSchedTime(last_sched_time_);

    // Throttle outputs don't actually mix; they provide backpressure to the
    // pipeline by holding AudioPacket references until they are presented. We
    // only need to schedule our next callback to keep things running, and let
    // the base class implementation handle trimming the output.
    return false;
  }

  bool FinishMixJob(const MixJob& job) override {
    // Since we never start any jobs, this should never be called.
    FXL_DCHECK(false);
    return false;
  }

  // AudioDevice implementation.
  // No one should ever be trying to apply gain limits for a throttle output.
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) override {
    FXL_DCHECK(false);
  }

 private:
  fxl::TimePoint last_sched_time_;
  bool uninitialized_ = true;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_THROTTLE_OUTPUT_H_
