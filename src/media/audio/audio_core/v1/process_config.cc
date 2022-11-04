// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/process_config.h"

#include <optional>
#include <utility>
#include <vector>

#include "src/media/audio/audio_core/v1/mix_profile_config.h"

namespace media::audio {

ProcessConfigBuilder& ProcessConfigBuilder::SetDefaultVolumeCurve(VolumeCurve curve) {
  default_volume_curve_ = {curve};
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

  output_device_profiles_.emplace_back(std::move(*device_id), profile);
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

  input_device_profiles_.emplace_back(std::move(*device_id), profile);
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::SetMixProfile(MixProfileConfig mix_profile_config) {
  mix_profile_config_ = mix_profile_config;
  FX_LOGS(INFO) << "Setting a custom MixProfile: capacity_usec "
                << mix_profile_config.capacity.to_usecs() << "; deadline_usec "
                << mix_profile_config.deadline.to_usecs() << "; period_usec "
                << mix_profile_config.period.to_usecs();
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::AddThermalConfigState(
    ThermalConfig::State thermal_config_state) {
  thermal_config_states_.push_back(std::move(thermal_config_state));
  return *this;
}

ProcessConfig ProcessConfigBuilder::Build() {
  FX_CHECK(default_volume_curve_) << "Missing required VolumeCurve member";
  return ProcessConfig(
      *default_volume_curve_,
      DeviceConfig(std::move(output_device_profiles_), std::move(default_output_device_profile_),
                   std::move(input_device_profiles_), std::move(default_input_device_profile_),
                   *default_volume_curve_),
      mix_profile_config_, ThermalConfig(std::move(thermal_config_states_)));
}

}  // namespace media::audio
