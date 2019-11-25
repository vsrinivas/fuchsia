// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef SRC_MEDIA_AUDIO_EXAMPLES_EFFECTS_EFFECT_BASE_H_
#define SRC_MEDIA_AUDIO_EXAMPLES_EFFECTS_EFFECT_BASE_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <stdint.h>

#include <memory>
#include <string_view>

namespace media::audio_effects_example {

enum Effect : uint32_t { Delay = 0, Rechannel = 1, Swap = 2, Count = 3 };

class EffectBase {
 public:
  static constexpr uint16_t kNumTestEffects = Effect::Count;

  static bool GetNumEffects(uint32_t* num_effects_out);
  static bool GetInfo(uint32_t effect_id, fuchsia_audio_effects_description* desc);

  static EffectBase* Create(uint32_t effect_id, uint32_t frame_rate, uint16_t channels_in,
                            uint16_t channels_out, std::string_view config);

  EffectBase(uint32_t effect_id, uint32_t frame_rate, uint16_t channels_in, uint16_t channels_out,
             uint32_t frames_latency, uint32_t suggested_buff_frames)
      : effect_id_(effect_id),
        frame_rate_(frame_rate),
        channels_in_(channels_in),
        channels_out_(channels_out),
        frames_latency_(frames_latency),
        suggested_buff_frames_(suggested_buff_frames) {}

  virtual ~EffectBase() = default;

  bool GetParameters(fuchsia_audio_effects_parameters* device_fx_params);

  virtual bool UpdateConfiguration(std::string_view) { return false; }

  virtual bool ProcessInplace(uint32_t, float*) { return false; }
  virtual bool Process(uint32_t, const float*, float*) { return false; }
  virtual bool Flush() { return true; }

 protected:
  uint32_t effect_id_;

  uint32_t frame_rate_;
  uint16_t channels_in_;
  uint16_t channels_out_;
  uint32_t frames_latency_;
  uint32_t suggested_buff_frames_;
};

}  // namespace media::audio_effects_example

#endif  // SRC_MEDIA_AUDIO_EXAMPLES_EFFECTS_EFFECT_BASE_H_
