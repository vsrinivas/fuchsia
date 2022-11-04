// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/loudness_transform.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/lib/processing/gain.h"

namespace media::audio {

float MappedLoudnessTransform::EvaluateStageGain(const LoudnessTransform::Stage& stage) const {
  if (auto volume = std::get_if<VolumeValue>(&stage)) {
    return volume_curve_.VolumeToDb(volume->value);
  } else if (auto gain = std::get_if<GainDbFsValue>(&stage)) {
    return gain->value;
  } else if (auto gain = std::get_if<GainToVolumeValue>(&stage)) {
    return volume_curve_.DbToVolume(gain->value);
  } else {
    FX_LOGS_FIRST_N(ERROR, 10) << "A loudness variant was uninitialized.";
    return media_audio::kUnityGainDb;
  }
}

float NoOpLoudnessTransform::EvaluateStageGain(const LoudnessTransform::Stage& stages) const {
  return media_audio::kUnityGainDb;
}

}  // namespace media::audio
