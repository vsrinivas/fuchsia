// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef LIB_MEDIA_AUDIO_DFX_LIB_DFX_RECHANNEL_H_
#define LIB_MEDIA_AUDIO_DFX_LIB_DFX_RECHANNEL_H_

#include <stdint.h>

#include "garnet/public/lib/media/audio_dfx/audio_device_fx.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_base.h"

namespace media {
namespace audio_dfx_test {

// DfxRechannel: an example of non-in-place effect with no controls. Being non-
// inplace, it has channel restrictions: specifically it must take in six
// channels and produce two channels. It does so while adding no latency.
class DfxRechannel : public DfxBase {
 public:
  static constexpr uint16_t kNumControls = 0;
  static constexpr uint16_t kNumChannelsIn = 6;
  static constexpr uint16_t kNumChannelsOut = 2;
  static constexpr uint32_t kLatencyFrames = 0;

  static bool GetInfo(fuchsia_audio_dfx_description* dfx_desc) {
    std::strcpy(dfx_desc->name, "5.1 to Stereo");
    dfx_desc->num_controls = kNumControls;
    dfx_desc->incoming_channels = kNumChannelsIn;
    dfx_desc->outgoing_channels = kNumChannelsOut;
    return true;
  }

  static bool GetControlInfo(uint16_t, fuchsia_audio_dfx_control_description*) {
    return false;
  }

  static DfxRechannel* Create(uint32_t frame_rate, uint16_t channels_in,
                              uint16_t channels_out) {
    return (channels_in == kNumChannelsIn && channels_out == kNumChannelsOut
                ? new DfxRechannel(frame_rate)
                : nullptr);
  }

  DfxRechannel(uint32_t frame_rate)
      : DfxBase(Effect::Rechannel, kNumControls, frame_rate, kNumChannelsIn,
                kNumChannelsOut, kLatencyFrames, kLatencyFrames) {}

  // Effect converts a 5.1 mix into stereo.
  // Left  = FL + FC*sqr(.5) + BL  -and-  Right = FR + FC*sqr(.5) + BR
  // To normalize: div by (1+.7071+1) or *= .36939806251812928
  // Note: LFE is omitted (common practice in stereo downmixes).
  // Or, with dpl encoding:
  // Left  = FL + FC*sqr(.5) + BL*sqr(.75) + BR*sqr(.25)
  // Right = FR + FC*sqr(.5) - BL*sqr(.25) - BR*sqr(.75)
  // To normalize: div by (1+.7071+.8660+.5) or *= .32540090689572506
  bool Process(uint32_t num_frames, const float* buff_in, float* buff_out) {
    for (uint32_t frame = 0; frame < num_frames; ++frame) {
      uint32_t out = frame * channels_out_;
      uint32_t in = frame * channels_in_;
      if (!encode_) {
        buff_out[out] =
            (buff_in[in] + (0.707106781f * buff_in[in + 2]) + buff_in[in + 4]) *
            0.369398062f;
        buff_out[out + 1] =
            (buff_in[in + 1] + (0.707106781f * buff_in[in + 2]) +
             buff_in[in + 5]) *
            0.369398062f;
      } else {
        buff_out[out] =
            (buff_in[in] + (0.707106781f * buff_in[in + 2]) +
             (0.866025403f * buff_in[in + 4]) + (0.5f * buff_in[in + 5])) *
            0.325400906f;
        buff_out[out + 1] =
            (buff_in[in + 1] + (0.707106781f * buff_in[in + 2]) -
             (0.5f * buff_in[in + 4]) - (0.866025403f * buff_in[in + 5])) *
            0.325400906f;
      }
    }
    return true;
  }

 private:
  bool encode_ = false;
};

}  // namespace audio_dfx_test
}  // namespace media

#endif  // LIB_MEDIA_AUDIO_DFX_LIB_DFX_RECHANNEL_H_
