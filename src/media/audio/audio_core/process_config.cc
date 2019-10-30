// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config.h"

#include "src/lib/fxl/logging.h"

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

ProcessConfig ProcessConfigBuilder::Build() {
  std::optional<VolumeCurve> maybe_curve = std::nullopt;
  default_volume_curve_.swap(maybe_curve);
  FXL_CHECK(maybe_curve) << "Missing required VolumeCurve member";
  return {std::move(*maybe_curve), std::move(pipeline_)};
}

}  // namespace media::audio
