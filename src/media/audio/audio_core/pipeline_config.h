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
  };

  // Returns the set of effects that are to be used for the provided output stream.
  const std::vector<MixGroup>& GetOutputStreams() const { return output_streams_; }

  // Returns the set of effects to be applied on the mix stage.
  const MixGroup& GetMix() const { return mix_; }

  // Returns the set of effects to be applied on the linearize stage.
  const MixGroup& GetLinearize() const { return linearize_; }

 private:
  friend class ProcessConfigBuilder;

  std::vector<MixGroup> output_streams_;
  MixGroup mix_;
  MixGroup linearize_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_PIPELINE_CONFIG_H_
