// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config.h"

#include <optional>
#include <utility>
#include <vector>

#include "src/media/audio/audio_core/mix_profile_config.h"

namespace media::audio {

// static
std::optional<ProcessConfig> ProcessConfig::instance_;

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
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::AddThermalPolicyEntry(
    ThermalConfig::Entry thermal_policy_entry) {
  thermal_config_entries_.push_back(std::move(thermal_policy_entry));
  return *this;
}

ProcessConfigBuilder& ProcessConfigBuilder::AddThermalNominalState(
    ThermalConfig::StateTransition nominal_state) {
  for (auto& state : thermal_nominal_states_) {
    FX_CHECK(nominal_state.target_name() != state.target_name())
        << "Only one nominal state per target allowed";
  }
  thermal_nominal_states_.push_back(std::move(nominal_state));
  return *this;
}

ProcessConfig ProcessConfigBuilder::Build() {
  std::optional<VolumeCurve> maybe_curve = std::nullopt;
  default_volume_curve_.swap(maybe_curve);
  FX_CHECK(maybe_curve) << "Missing required VolumeCurve member";
  return ProcessConfig(
      std::move(*maybe_curve),
      DeviceConfig(std::move(output_device_profiles_), std::move(default_output_device_profile_),
                   std::move(input_device_profiles_), std::move(default_input_device_profile_)),
      mix_profile_config_,
      ThermalConfig(std::move(thermal_config_entries_), std::move(thermal_nominal_states_)));
}

}  // namespace media::audio
