// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef LIB_MEDIA_AUDIO_DFX_LIB_DFX_BASE_H_
#define LIB_MEDIA_AUDIO_DFX_LIB_DFX_BASE_H_

#include <stdint.h>
#include <memory>

#include "garnet/public/lib/media/audio_dfx/audio_device_fx.h"

namespace media {
namespace audio_dfx_test {

enum Effect : uint32_t { Delay = 0, Rechannel = 1, Swap = 2, Count = 3 };

class DfxBase {
 public:
  static constexpr uint16_t kNumTestEffects = Effect::Count;

  static bool GetNumEffects(uint32_t* num_effects_out);
  static bool GetInfo(uint32_t effect_id,
                      fuchsia_audio_dfx_description* dfx_desc);
  static bool GetControlInfo(
      uint32_t effect_id, uint16_t control_num,
      fuchsia_audio_dfx_control_description* dfx_control_desc);

  static DfxBase* Create(uint32_t effect_id, uint32_t frame_rate,
                         uint16_t channels_in, uint16_t channels_out);

  DfxBase(uint32_t effect_id, uint16_t num_controls, uint32_t frame_rate,
          uint16_t channels_in, uint16_t channels_out, uint32_t frames_latency,
          uint32_t suggested_buff_frames)
      : effect_id_(effect_id),
        num_controls_(num_controls),
        frame_rate_(frame_rate),
        channels_in_(channels_in),
        channels_out_(channels_out),
        frames_latency_(frames_latency),
        suggested_buff_frames_(suggested_buff_frames) {}

  virtual ~DfxBase() = default;

  bool GetParameters(fuchsia_audio_dfx_parameters* device_fx_params);

  virtual bool GetControlValue(uint16_t, float*) { return false; }
  virtual bool SetControlValue(uint16_t, float) { return false; }
  virtual bool Reset() { return true; }

  virtual bool ProcessInplace(uint32_t, float*) { return false; }
  virtual bool Process(uint32_t, const float*, float*) { return false; }
  virtual bool Flush() { return true; }

  uint16_t num_controls() { return num_controls_; }

 protected:
  uint32_t effect_id_;
  uint16_t num_controls_;

  uint32_t frame_rate_;
  uint16_t channels_in_;
  uint16_t channels_out_;
  uint32_t frames_latency_;
  uint32_t suggested_buff_frames_;
};

}  // namespace audio_dfx_test
}  // namespace media

#endif  // LIB_MEDIA_AUDIO_DFX_LIB_DFX_BASE_H_
