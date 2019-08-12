// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef EXAMPLES_MEDIA_AUDIO_EFFECTS_DFX_DELAY_H_
#define EXAMPLES_MEDIA_AUDIO_EFFECTS_DFX_DELAY_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <stdint.h>

#include <memory>

#include "examples/media/audio/effects/dfx_base.h"

namespace media::audio_dfx_test {

// DfxDelay: example of inplace effect with one control. Channels_in must always
// equal channels_out, but it has no further restriction. This effect delays all
// channels by a constant number of frames (specified by the control setting).
//
// This effect INTENTIONALLY adds a delay which clock-synchronization mechanisms
// should NOT try to compensate for; in fact it adds zero "unwanted" latency.
class DfxDelay : public DfxBase {
 public:
  static constexpr uint16_t kNumChannelsIn = FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY;
  static constexpr uint16_t kNumChannelsOut = FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN;
  static constexpr uint32_t kLatencyFrames = 0;

  static constexpr uint32_t kMaxDelayFrames = 64000;
  static constexpr uint32_t kMinDelayFrames = 0;

  static bool GetInfo(fuchsia_audio_effects_description* dfx_desc);

  static DfxDelay* Create(uint32_t frame_rate, uint16_t channels_in, uint16_t channels_out,
                          std::string_view config);

  DfxDelay(uint32_t frame_rate, uint16_t channels, uint32_t delay_frames);
  ~DfxDelay() = default;

  bool UpdateConfiguration(std::string_view config) override;

  bool ProcessInplace(uint32_t num_frames, float* audio_buff) override;
  bool Flush() override;

 protected:
  uint32_t delay_samples_;
  // Buffer must accommodate the largest process_inplace call, plus our delay.
  // N.B.: 'num_frames' for process_inplace can be as large as frame_rate.
  std::unique_ptr<float[]> delay_buff_;
};

}  // namespace media::audio_dfx_test

#endif  // EXAMPLES_MEDIA_AUDIO_EFFECTS_DFX_DELAY_H_
