// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_PIPELINE_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_PIPELINE_CONFIG_H_

#include <fuchsia/media/cpp/fidl.h>

#include <string>
#include <vector>

namespace media::audio {

class PipelineConfig {
 public:
  struct Effect {
    // The name of the shared object to load the effect from.
    std::string lib_name;

    // The name of the effect to load from |lib_name|.
    std::string effect_name;

    // To be passed to the EffectLoader. This is an opaque string used to configure the effect
    // instance.
    std::string effect_config;
  };

  struct MixGroup {
    std::string name;
    std::vector<fuchsia::media::AudioRenderUsage> input_streams;
    std::vector<Effect> effects;
    std::vector<MixGroup> inputs;
  };

  static PipelineConfig Default() {
    PipelineConfig config;
    config.root_.name = "default";
    config.root_.input_streams = {
        fuchsia::media::AudioRenderUsage::BACKGROUND,
        fuchsia::media::AudioRenderUsage::MEDIA,
        fuchsia::media::AudioRenderUsage::INTERRUPTION,
        fuchsia::media::AudioRenderUsage::SYSTEM_AGENT,
        fuchsia::media::AudioRenderUsage::COMMUNICATION,
    };
    return config;
  }

  PipelineConfig() = default;
  explicit PipelineConfig(MixGroup root) : root_(std::move(root)) {}

  const MixGroup& root() const { return root_; }

 private:
  friend class ProcessConfigBuilder;

  MixGroup root_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PIPELINE_CONFIG_H_
