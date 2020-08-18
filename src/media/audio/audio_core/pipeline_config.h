// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PIPELINE_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PIPELINE_CONFIG_H_

#include <string>
#include <vector>

#include "src/media/audio/audio_core/stream_usage.h"

namespace media::audio {

class PipelineConfig {
 public:
  static constexpr uint32_t kDefaultMixGroupRate = 48000;
  static constexpr uint16_t kDefaultMixGroupChannels = 2;

  struct Effect {
    // The name of the shared object to load the effect from.
    std::string lib_name;

    // The name of the effect to load from |lib_name|.
    std::string effect_name;

    // A name for the specific effect instance. Primarily for diagnostic purposes.
    std::string instance_name;

    // To be passed to the EffectLoader. This is an opaque string used to configure the effect
    // instance.
    std::string effect_config;

    // The number of output channels for this effect. If |std::nullopt|, then output channels will
    // match the number of input channels.
    std::optional<uint16_t> output_channels;
  };

  struct MixGroup {
    std::string name;
    std::vector<RenderUsage> input_streams;
    std::vector<Effect> effects;
    std::vector<MixGroup> inputs;
    bool loopback = false;
    uint32_t output_rate = kDefaultMixGroupRate;
    uint16_t output_channels = kDefaultMixGroupChannels;
  };

  static PipelineConfig Default(uint32_t frame_rate = kDefaultMixGroupRate,
                                uint16_t channels = kDefaultMixGroupChannels) {
    PipelineConfig config;
    config.root_.name = "default";
    config.root_.input_streams = {
        RenderUsage::BACKGROUND,   RenderUsage::MEDIA,         RenderUsage::INTERRUPTION,
        RenderUsage::SYSTEM_AGENT, RenderUsage::COMMUNICATION,
    };
    config.root_.output_rate = frame_rate;
    config.root_.output_channels = channels;
    config.root_.loopback = true;
    return config;
  }

  PipelineConfig() = default;
  explicit PipelineConfig(MixGroup root) : root_(std::move(root)) {}

  const MixGroup& root() const { return root_; }

  MixGroup& mutable_root() { return root_; }

  uint16_t channels() const;
  uint32_t frames_per_second() const { return root_.output_rate; }

 private:
  friend class ProcessConfigBuilder;

  MixGroup root_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PIPELINE_CONFIG_H_
