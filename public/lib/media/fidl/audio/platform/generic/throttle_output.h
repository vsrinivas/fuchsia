// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_AUDIO_PLATFORM_GENERIC_THROTTLE_OUTPUT_H_
#define SERVICES_MEDIA_AUDIO_PLATFORM_GENERIC_THROTTLE_OUTPUT_H_

#include "base/callback.h"
#include "mojo/services/media/common/cpp/local_time.h"
#include "services/media/audio/platform/generic/standard_output_base.h"

namespace mojo {
namespace media {
namespace audio {

class ThrottleOutput : public StandardOutputBase {
 public:
  static AudioOutputPtr New(AudioOutputManager* manager);
  ~ThrottleOutput() override;

 protected:
  explicit ThrottleOutput(AudioOutputManager* manager);

  // AudioOutput Implementation
  MediaResult Init() override;

  // StandardOutputBase Implementation
  bool StartMixJob(MixJob* job, const LocalTime& process_start) override;
  bool FinishMixJob(const MixJob& job) override;

 private:
  LocalTime last_sched_time_;
};

}  // namespace audio
}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_AUDIO_PLATFORM_GENERIC_THROTTLE_OUTPUT_H_
