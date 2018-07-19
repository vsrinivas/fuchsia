// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef LIB_MEDIA_AUDIO_DFX_LIB_DFX_SWAP_H_
#define LIB_MEDIA_AUDIO_DFX_LIB_DFX_SWAP_H_

#include <stdint.h>

#include "garnet/public/lib/media/audio_dfx/audio_device_fx.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_base.h"

namespace media {
namespace audio_dfx_test {

// DfxSwap: an example of an in-place effect with no controls. It has a channel
// restriction: it must be stereo-in and stereo-out. This effect swaps the left
// and right channels, and does so without adding latency.
class DfxSwap : public DfxBase {
 public:
  static constexpr uint16_t kNumControls = 0;
  static constexpr uint16_t kNumChannelsIn = 2;
  static constexpr uint16_t kNumChannelsOut = 2;
  static constexpr uint32_t kLatencyFrames = 0;

  static bool GetInfo(fuchsia_audio_dfx_description* dfx_desc) {
    std::strcpy(dfx_desc->name, "Left-Right Swap");
    dfx_desc->num_controls = kNumControls;
    dfx_desc->incoming_channels = kNumChannelsIn;
    dfx_desc->outgoing_channels = kNumChannelsOut;
    return true;
  }

  static bool GetControlInfo(uint16_t, fuchsia_audio_dfx_control_description*) {
    return false;
  }

  static DfxSwap* Create(uint32_t frame_rate, uint16_t channels_in,
                         uint16_t channels_out) {
    return (channels_in == kNumChannelsIn && channels_out == kNumChannelsOut
                ? new DfxSwap(frame_rate, channels_in)
                : nullptr);
  }

  DfxSwap(uint32_t frame_rate, uint16_t channels)
      : DfxBase(Effect::Swap, kNumControls, frame_rate, channels, channels,
                kLatencyFrames, kLatencyFrames) {}

  bool ProcessInplace(uint32_t num_frames, float* audio_buff) {
    for (uint32_t sample = 0; sample < num_frames * channels_in_;
         sample += channels_in_) {
      float temp = audio_buff[sample];
      audio_buff[sample] = audio_buff[sample + 1];
      audio_buff[sample + 1] = temp;
    }
    return true;
  }
};

}  // namespace audio_dfx_test
}  // namespace media

#endif  // LIB_MEDIA_AUDIO_DFX_LIB_DFX_SWAP_H_
