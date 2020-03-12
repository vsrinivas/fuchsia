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
  };

  struct MixGroup {
    std::string name;
    std::vector<RenderUsage> input_streams;
    std::vector<Effect> effects;
    std::vector<MixGroup> inputs;
    bool loopback;
    uint32_t output_rate;
  };

  static PipelineConfig Default() {
    PipelineConfig config;
    config.root_.name = "default";
    config.root_.input_streams = {
        RenderUsage::BACKGROUND,   RenderUsage::MEDIA,         RenderUsage::INTERRUPTION,
        RenderUsage::SYSTEM_AGENT, RenderUsage::COMMUNICATION,
    };
    config.root_.output_rate = kDefaultMixGroupRate;
    config.root_.loopback = true;
    return config;
  }

  PipelineConfig() = default;
  explicit PipelineConfig(MixGroup root) : root_(std::move(root)) {}

  const MixGroup& root() const { return root_; }

  MixGroup& mutable_root() { return root_; }

 private:
  friend class ProcessConfigBuilder;

  MixGroup root_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PIPELINE_CONFIG_H_
