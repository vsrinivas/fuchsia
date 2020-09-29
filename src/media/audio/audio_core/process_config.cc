// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config.h"

namespace media::audio {

// static
std::optional<ProcessConfig> ProcessConfig::instance_;

ProcessConfigBuilder& ProcessConfigBuilder::SetDefaultVolumeCurve(VolumeCurve curve) {
  default_volume_curve_ = {curve};
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::SetDefaultRenderUsageVolumes(
    RenderUsageVolumes volumes) {
  default_render_usage_volumes_ = {volumes};
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::AddDeviceProfile(
    std::pair<std::optional<std::vector<audio_stream_unique_id_t>>,
              DeviceConfig::OutputDeviceProfile>
        keyed_profile) {
  auto& [device_id, profile] = keyed_profile;
  if (!device_id.has_value()) {
    FX_CHECK(!default_output_device_profile_.has_value())
        << "Config specifies two default output usage support sets; must have only one.";
    default_output_device_profile_ = std::move(profile);
    return *this;
  }

  output_device_profiles_.push_back({std::move(*device_id), profile});
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::AddDeviceProfile(
    std::pair<std::optional<std::vector<audio_stream_unique_id_t>>,
              DeviceConfig::InputDeviceProfile>
        keyed_profile) {
  auto& [device_id, profile] = keyed_profile;
  if (!device_id.has_value()) {
    FX_CHECK(!default_input_device_profile_.has_value())
        << "Config specifies two default input profiles; must have only one.";
    default_input_device_profile_ = std::move(profile);
    return *this;
  }

  input_device_profiles_.push_back({std::move(*device_id), profile});
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::AddThermalPolicyEntry(
    ThermalConfig::Entry thermal_policy_entry) {
  thermal_config_entries_.push_back(std::move(thermal_policy_entry));
  return *this;
}

ProcessConfig ProcessConfigBuilder::Build() {
  std::optional<VolumeCurve> maybe_curve = std::nullopt;
  default_volume_curve_.swap(maybe_curve);
  FX_CHECK(maybe_curve) << "Missing required VolumeCurve member";
  return ProcessConfig(
      std::move(*maybe_curve), std::move(default_render_usage_volumes_),
      DeviceConfig(std::move(output_device_profiles_), std::move(default_output_device_profile_),
                   std::move(input_device_profiles_), std::move(default_input_device_profile_)),
      ThermalConfig(std::move(thermal_config_entries_)));
}

}  // namespace media::audio
