// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PIPELINE_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PIPELINE_CONFIG_H_

#include <optional>
#include <string>
#include <vector>

#include "src/media/audio/audio_core/v1/stream_usage.h"
#include "src/media/audio/lib/effects_loader/effects_loader_v2.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio {

class PipelineConfig {
 public:
  static constexpr int32_t kDefaultMixGroupRate = 48000;
  static constexpr int16_t kDefaultMixGroupChannels = 2;

  // An effect that uses the API defined at
  // sdk/lib/media/audio/effects/audio_effects.h
  struct EffectV1 {
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

  // An effect that uses the API defined at
  // sdk/fidl/fuchsia.audio.effects/factory.fidl
  struct EffectV2 {
    // The name of the effect to load from fuchsia.audio.effects.ProcessorCreator/Create.
    std::string instance_name;
  };

  struct MixGroup {
    std::string name;
    std::vector<RenderUsage> input_streams;
    // Either effects_v1 or effects_v2 may be specified, but not both.
    // For V1, we allow a sequence of effects, while for V2, there is
    // at most one effect per mix group (if a sequence of effects is
    // needed, the sequence must be implemented behind the FIDL call).
    std::vector<EffectV1> effects_v1;
    std::optional<EffectV2> effects_v2;
    std::vector<MixGroup> inputs;
    std::optional<float> min_gain_db;
    std::optional<float> max_gain_db;
    bool loopback = false;
    // TODO(fxbug.dev/70642): rename these fields require that they be specified explicitly
    int32_t output_rate = kDefaultMixGroupRate;
    int16_t output_channels = kDefaultMixGroupChannels;
  };

  static PipelineConfig Default(int32_t frame_rate = kDefaultMixGroupRate,
                                int16_t channels = kDefaultMixGroupChannels) {
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

  MixGroup& mutable_root() {
    channels_ = std::nullopt;  // might be about to change
    return root_;
  }

  // Compute this pipeline's output format. The sample format is always FLOAT.
  // The given loader is used to obtain complete information about V2 effect formats.
  // The loader may be nullptr if the PipelineConfig does not contain any V2 effects.
  Format OutputFormat(EffectsLoaderV2* effects_loader_v2) const;

 private:
  friend class ProcessConfigBuilder;
  uint32_t OutputChannels(EffectsLoaderV2* effects_loader_v2) const;

  MixGroup root_;
  mutable std::optional<int64_t> channels_;  // memoized channels()
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PIPELINE_CONFIG_H_
