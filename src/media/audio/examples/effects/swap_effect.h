// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef SRC_MEDIA_AUDIO_EXAMPLES_EFFECTS_SWAP_EFFECT_H_
#define SRC_MEDIA_AUDIO_EXAMPLES_EFFECTS_SWAP_EFFECT_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <stdint.h>

#include "src/media/audio/examples/effects/effect_base.h"

namespace media::audio_effects_example {

// SwapEffect: an example of an in-place effect with no controls. It has a channel
// restriction: it must be stereo-in and stereo-out. This effect swaps the left
// and right channels, and does so without adding latency.
class SwapEffect : public EffectBase {
 public:
  static constexpr uint16_t kNumChannelsIn = 2;
  static constexpr uint16_t kNumChannelsOut = 2;
  static constexpr uint32_t kLatencyFrames = 0;

  static bool GetInfo(fuchsia_audio_effects_description* desc) {
    std::strcpy(desc->name, "Left-Right Swap");
    desc->incoming_channels = kNumChannelsIn;
    desc->outgoing_channels = kNumChannelsOut;
    return true;
  }

  static SwapEffect* Create(uint32_t frame_rate, uint16_t channels_in, uint16_t channels_out,
                            std::string_view) {
    return (channels_in == kNumChannelsIn && channels_out == kNumChannelsOut
                ? new SwapEffect(frame_rate, channels_in)
                : nullptr);
  }

  SwapEffect(uint32_t frame_rate, uint16_t channels)
      : EffectBase(Effect::Swap, frame_rate, channels, channels, kLatencyFrames, kLatencyFrames) {}

  bool ProcessInplace(uint32_t num_frames, float* audio_buff) {
    for (uint32_t sample = 0; sample < num_frames * channels_in_; sample += channels_in_) {
      float temp = audio_buff[sample];
      audio_buff[sample] = audio_buff[sample + 1];
      audio_buff[sample + 1] = temp;
    }
    return true;
  }
};

}  // namespace media::audio_effects_example

#endif  // SRC_MEDIA_AUDIO_EXAMPLES_EFFECTS_SWAP_EFFECT_H_
