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
    std::pair<std::optional<audio_stream_unique_id_t>, RoutingConfig::UsageSupportSet> profile) {
  auto& [device_id, output_usage_support_set] = profile;
  if (!device_id.has_value()) {
    FX_CHECK(!routing_config_.default_output_usage_support_set_.has_value())
        << "Config specifies two default output usage support sets; must have only one.";
    routing_config_.default_output_usage_support_set_ = std::move(output_usage_support_set);
    return *this;
  }

  routing_config_.device_output_usage_support_sets_.push_back(
      {std::move(*device_id), std::move(output_usage_support_set)});
  return *this;
}

ProcessConfig ProcessConfigBuilder::Build() {
  std::optional<VolumeCurve> maybe_curve = std::nullopt;
  default_volume_curve_.swap(maybe_curve);
  FX_CHECK(maybe_curve) << "Missing required VolumeCurve member";
  return {std::move(*maybe_curve), std::move(pipeline_), std::move(routing_config_)};
}

}  // namespace media::audio
