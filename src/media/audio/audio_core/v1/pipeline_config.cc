// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/pipeline_config.h"

namespace media::audio {

Format PipelineConfig::OutputFormat(EffectsLoaderV2* effects_loader_v2) const {
  auto channels = OutputChannels(effects_loader_v2);
  auto result = Format::Create({
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = channels,
      .frames_per_second = static_cast<uint32_t>(root_.output_rate),
  });
  FX_CHECK(result.is_ok()) << "bad format: channels=" << channels << " fps=" << root_.output_rate;
  return result.value();
}

uint32_t PipelineConfig::OutputChannels(EffectsLoaderV2* effects_loader_v2) const {
  // If no effect performs rechannelization, then our channelization is determined
  // by the mix stage itself.
  const auto default_output_channels = root_.output_channels;

  if (!root_.effects_v1.empty()) {
    // The bottommost effect that defines output_channels will define our channelization.
    for (auto it = root_.effects_v1.rbegin(); it != root_.effects_v1.rend(); ++it) {
      if (it->output_channels) {
        return *it->output_channels;
      }
    }
  } else if (root_.effects_v2.has_value()) {
    FX_CHECK(effects_loader_v2);
    // Loading this effect will create a shared channel and VMOs to communicate with the
    // FIDL server. We immediately drop those channels here. This is slightly wasteful,
    // but simpler than trying to memoize this config so it can be loaded just once.
    const std::string& name = root_.effects_v2->instance_name;
    auto config_result = effects_loader_v2->GetProcessorConfiguration(name);
    if (!config_result.ok() || config_result->is_error()) {
      auto status = !config_result.ok() ? config_result.status() : config_result->error_value();
      FX_PLOGS(ERROR, status) << "Cannot load V2 effect '" << name << "'";
      return default_output_channels;
    }
    auto& config = config_result->value()->processor_configuration;
    if (!config.has_outputs() || config.outputs().count() != 1 ||
        !config.outputs()[0].has_format()) {
      FX_LOGS(ERROR) << "V2 effect '" << name << "'"
                     << "must have exactly one output with a defined format";
      return default_output_channels;
    }
    return config.outputs()[0].format().channel_count;
  }

  return default_output_channels;
}

}  // namespace media::audio
