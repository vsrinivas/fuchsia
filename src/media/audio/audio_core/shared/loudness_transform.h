// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_LOUDNESS_TRANSFORM_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_LOUDNESS_TRANSFORM_H_

#include <fuchsia/media/cpp/fidl.h>

#include <variant>

#include "src/media/audio/audio_core/shared/mixer/gain.h"
#include "src/media/audio/audio_core/shared/volume_curve.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media::audio {

class MappedLoudnessTransform;

struct VolumeValue {
  float value;
};

struct GainDbFsValue {
  float value;
};

struct GainToVolumeValue {
  float value;
};

// A loudness transform considers many stages of loudness that apply to a stream,
// including volume settings and gain adjustments, and applies them sequentially.
class LoudnessTransform {
 public:
  virtual ~LoudnessTransform() = default;

  using Stage = std::variant<VolumeValue, GainDbFsValue, GainToVolumeValue>;

  // Sequentially evaluates each loudness stage and returns the gain to use for
  // the stream.
  template <int N>
  float Evaluate(std::array<Stage, N> stages) const {
    float gain = media_audio::kUnityGainDb;

    for (const auto& stage : stages) {
      auto next_stage = EvaluateStageGain(stage);
      gain = Gain::CombineGains(gain, next_stage);
    }

    return gain;
  }

  virtual float EvaluateStageGain(const Stage& stage) const = 0;
};

// Implements `LoudnessTransform` using a volume curve to map volume settings to
// gain in dbfs.
class MappedLoudnessTransform final : public LoudnessTransform {
 public:
  // The `volume_curve` must live as long as this transform.
  explicit MappedLoudnessTransform(const VolumeCurve& volume_curve) : volume_curve_(volume_curve) {}

  float EvaluateStageGain(const LoudnessTransform::Stage& stages) const override;

 private:
  const VolumeCurve volume_curve_;
};

// A `LoudnessTransform` that always returns unity gain, no matter what loudness stages are given.
class NoOpLoudnessTransform final : public LoudnessTransform {
  float EvaluateStageGain(const LoudnessTransform::Stage& stages) const final;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_LOUDNESS_TRANSFORM_H_
