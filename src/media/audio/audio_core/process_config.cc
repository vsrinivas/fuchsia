// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config.h"

namespace media::audio {

// static
std::optional<ProcessConfig> ProcessConfig::instance_;

ProcessConfigBuilder& ProcessConfigBuilder::SetMixEffects(PipelineConfig::MixGroup effects) {
  pipeline_.mix_ = std::move(effects);
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::SetLinearizeEffects(PipelineConfig::MixGroup effects) {
  pipeline_.linearize_ = std::move(effects);
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::AddOutputStreamEffects(
    PipelineConfig::MixGroup effects) {
  pipeline_.output_streams_.emplace_back(std::move(effects));
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::SetDefaultVolumeCurve(VolumeCurve curve) {
  default_volume_curve_ = {curve};
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::AddDeviceRoutingProfile(
    std::pair<std::optional<audio_stream_unique_id_t>, RoutingConfig::DeviceProfile>
        keyed_profile) {
  auto& [device_id, profile] = keyed_profile;
  if (!device_id.has_value()) {
    FX_CHECK(!default_device_profile_.has_value())
        << "Config specifies two default output usage support sets; must have only one.";
    default_device_profile_ = std::move(profile);
    return *this;
  }

  device_profiles_.push_back({std::move(*device_id), profile});
  return *this;
}

ProcessConfig ProcessConfigBuilder::Build() {
  std::optional<VolumeCurve> maybe_curve = std::nullopt;
  default_volume_curve_.swap(maybe_curve);
  FX_CHECK(maybe_curve) << "Missing required VolumeCurve member";
  return {std::move(*maybe_curve), std::move(pipeline_),
          RoutingConfig(std::move(device_profiles_), std::move(default_device_profile_))};
}

}  // namespace media::audio
