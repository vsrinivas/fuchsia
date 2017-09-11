// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/platform/generic/standard_output_base.h"

namespace media {
namespace audio {

class ThrottleOutput : public StandardOutputBase {
 public:
  static AudioOutputPtr Create(AudioOutputManager* manager);
  ~ThrottleOutput() override;

 protected:
  explicit ThrottleOutput(AudioOutputManager* manager);

  // AudioOutput Implementation
  MediaResult Init() override;

  // StandardOutputBase Implementation
  bool StartMixJob(MixJob* job, fxl::TimePoint process_start) override;
  bool FinishMixJob(const MixJob& job) override;

 private:
  fxl::TimePoint last_sched_time_;
};

}  // namespace audio
}  // namespace media
