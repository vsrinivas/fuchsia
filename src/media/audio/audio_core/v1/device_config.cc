// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/device_config.h"

namespace media::audio {

namespace {

const PipelineConfig::EffectV1* FindEffectV1InMixGroup(const std::string& instance_name,
                                                       const PipelineConfig::MixGroup& mix_group) {
  for (auto& effect : mix_group.effects_v1) {
    if (effect.instance_name == instance_name) {
      return &effect;
    }
  }

  for (auto& input : mix_group.inputs) {
    auto effect = FindEffectV1InMixGroup(instance_name, input);
    if (effect) {
      return effect;
    }
  }

  return nullptr;
}

}  // namespace

const std::shared_ptr<LoudnessTransform>& DeviceConfig::DeviceProfile::loudness_transform() const {
  return loudness_transform_;
}

const std::shared_ptr<LoudnessTransform> DeviceConfig::OutputDeviceProfile::kNoOpTransform =
    std::make_shared<NoOpLoudnessTransform>();

const std::shared_ptr<LoudnessTransform>& DeviceConfig::OutputDeviceProfile::loudness_transform()
    const {
  if (independent_volume_control_) {
    return kNoOpTransform;
  }

  return DeviceProfile::loudness_transform();
}

const PipelineConfig::EffectV1* DeviceConfig::FindEffectV1(const std::string& instance_name) const {
  auto effect = FindEffectV1InMixGroup(instance_name,
                                       default_output_device_profile_.pipeline_config().root());
  if (effect) {
    return effect;
  }

  for (auto& [unused_stream_id, device_profile] : output_device_profiles_) {
    auto effect = FindEffectV1InMixGroup(instance_name, device_profile.pipeline_config().root());
    if (effect) {
      return effect;
    }
  }

  return nullptr;
}

}  // namespace media::audio
