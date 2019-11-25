// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef SRC_MEDIA_AUDIO_EXAMPLES_EFFECTS_DELAY_EFFECT_H_
#define SRC_MEDIA_AUDIO_EXAMPLES_EFFECTS_DELAY_EFFECT_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <stdint.h>

#include <memory>

#include "src/media/audio/examples/effects/effect_base.h"

namespace media::audio_effects_example {

// DelayEffect: example of inplace effect with one control. Channels_in must always
// equal channels_out, but it has no further restriction. This effect delays all
// channels by a constant number of frames (specified by the control setting).
//
// This effect INTENTIONALLY adds a delay which clock-synchronization mechanisms
// should NOT try to compensate for; in fact it adds zero "unwanted" latency.
class DelayEffect : public EffectBase {
 public:
  static constexpr uint16_t kNumChannelsIn = FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY;
  static constexpr uint16_t kNumChannelsOut = FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN;
  static constexpr uint32_t kLatencyFrames = 0;

  static constexpr uint32_t kMaxDelayFrames = 64000;
  static constexpr uint32_t kMinDelayFrames = 0;

  static bool GetInfo(fuchsia_audio_effects_description* desc);

  static DelayEffect* Create(uint32_t frame_rate, uint16_t channels_in, uint16_t channels_out,
                             std::string_view config);

  DelayEffect(uint32_t frame_rate, uint16_t channels, uint32_t delay_frames);
  ~DelayEffect() = default;

  bool UpdateConfiguration(std::string_view config) override;

  bool ProcessInplace(uint32_t num_frames, float* audio_buff) override;
  bool Flush() override;

 protected:
  uint32_t delay_samples_;
  // Buffer must accommodate the largest process_inplace call, plus our delay.
  // N.B.: 'num_frames' for process_inplace can be as large as frame_rate.
  std::unique_ptr<float[]> delay_buff_;
};

}  // namespace media::audio_effects_example

#endif  // SRC_MEDIA_AUDIO_EXAMPLES_EFFECTS_DELAY_EFFECT_H_
