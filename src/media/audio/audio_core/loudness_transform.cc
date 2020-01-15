// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/loudness_transform.h"

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {

float MappedLoudnessTransform::EvaluateStageGain(const LoudnessTransform::Stage& stage) const {
  if (auto volume = std::get_if<VolumeValue>(&stage)) {
    return volume_curve_.VolumeToDb(volume->value);
  } else if (auto gain = std::get_if<GainDbFsValue>(&stage)) {
    return gain->value;
  } else {
    FX_LOGS_FIRST_N(ERROR, 10) << "A loudness variant was uninitialized.";
    return Gain::kUnityGainDb;
  }
}

float NoOpLoudnessTransform::EvaluateStageGain(const LoudnessTransform::Stage& stages) const {
  return Gain::kUnityGainDb;
}

}  // namespace media::audio
