// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef LIB_MEDIA_AUDIO_DFX_LIB_DFX_DELAY_H_
#define LIB_MEDIA_AUDIO_DFX_LIB_DFX_DELAY_H_

#include <stdint.h>
#include <memory>

#include "garnet/public/lib/media/audio_dfx/audio_device_fx.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_base.h"

namespace media {
namespace audio_dfx_test {

// DfxDelay: example of inplace effect with one control. Channels_in must always
// equal channels_out, but it has no further restriction. This effect delays all
// channels by a constant number of frames (specified by the control setting).
//
// This effect INTENTIONALLY adds a delay which clock-synchronization mechanisms
// should NOT try to compensate for; in fact it adds zero "unwanted" latency.
class DfxDelay : public DfxBase {
 public:
  static constexpr uint16_t kNumControls = 1;
  static constexpr uint16_t kNumChannelsIn = FUCHSIA_AUDIO_DFX_CHANNELS_ANY;
  static constexpr uint16_t kNumChannelsOut =
      FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN;
  static constexpr uint32_t kLatencyFrames = 0;

  static constexpr uint32_t kMaxDelayFrames = 65536;
  static constexpr uint32_t kMinDelayFrames = 0;
  static constexpr uint32_t kInitialDelayFrames = 0;

  static bool GetInfo(fuchsia_audio_dfx_description* dfx_desc);
  static bool GetControlInfo(
      uint16_t control_num,
      fuchsia_audio_dfx_control_description* device_fx_control_desc);

  static DfxDelay* Create(uint32_t frame_rate, uint16_t channels_in,
                          uint16_t channels_out);

  DfxDelay(uint32_t frame_rate, uint16_t channels);

  bool GetControlValue(uint16_t control_num, float* value_out) override;
  bool SetControlValue(uint16_t control_num, float value) override;
  bool Reset() override;

  bool ProcessInplace(uint32_t num_frames, float* audio_buff) override;
  bool Flush() override;

 protected:
  uint32_t delay_samples_;
  std::unique_ptr<float[]> delay_buff_;
  std::unique_ptr<float[]> temp_buff_;
};

}  // namespace audio_dfx_test
}  // namespace media

#endif  // LIB_MEDIA_AUDIO_DFX_LIB_DFX_DELAY_H_
