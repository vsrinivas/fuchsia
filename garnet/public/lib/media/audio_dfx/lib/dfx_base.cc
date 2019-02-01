// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/media/audio_dfx/lib/dfx_base.h"

#include <fbl/algorithm.h>
#include <math.h>

#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/media/audio_dfx/audio_device_fx.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_delay.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_rechannel.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_swap.h"

namespace media {
namespace audio_dfx_test {

//
// DfxBase: static member functions
//

// static; satisfied by base class
bool DfxBase::GetNumEffects(uint32_t* num_effects_out) {
  *num_effects_out = Effect::Count;
  return true;
}

// static; dispatched by base class to the appropriate subclass static
bool DfxBase::GetInfo(uint32_t effect_id,
                      fuchsia_audio_dfx_description* dfx_desc) {
  switch (effect_id) {
    case media::audio_dfx_test::Effect::Delay:
      return media::audio_dfx_test::DfxDelay::GetInfo(dfx_desc);
    case media::audio_dfx_test::Effect::Rechannel:
      return media::audio_dfx_test::DfxRechannel::GetInfo(dfx_desc);
    case media::audio_dfx_test::Effect::Swap:
      return media::audio_dfx_test::DfxSwap::GetInfo(dfx_desc);
  }

  return false;
}

// static; dispatched by base class to the appropriate subclass static
bool DfxBase::GetControlInfo(
    uint32_t effect_id, uint16_t control_num,
    fuchsia_audio_dfx_control_description* dfx_control_desc) {
  switch (effect_id) {
    case media::audio_dfx_test::Effect::Rechannel:
      return media::audio_dfx_test::DfxRechannel::GetControlInfo(
          control_num, dfx_control_desc);
    case media::audio_dfx_test::Effect::Swap:
      return media::audio_dfx_test::DfxSwap::GetControlInfo(control_num,
                                                            dfx_control_desc);
    case media::audio_dfx_test::Effect::Delay:
      return media::audio_dfx_test::DfxDelay::GetControlInfo(control_num,
                                                             dfx_control_desc);
  }

  return false;
}

// static; dispatched by base class to the appropriate subclass static
DfxBase* DfxBase::Create(uint32_t effect_id, uint32_t frame_rate,
                         uint16_t channels_in, uint16_t channels_out) {
  if (channels_in > FUCHSIA_AUDIO_DFX_CHANNELS_MAX ||
      channels_out > FUCHSIA_AUDIO_DFX_CHANNELS_MAX) {
    return nullptr;
  }

  switch (effect_id) {
    case media::audio_dfx_test::Effect::Delay:
      return reinterpret_cast<DfxBase*>(
          DfxDelay::Create(frame_rate, channels_in, channels_out));

    case media::audio_dfx_test::Effect::Rechannel:
      return reinterpret_cast<DfxBase*>(
          DfxRechannel::Create(frame_rate, channels_in, channels_out));

    case media::audio_dfx_test::Effect::Swap:
      return reinterpret_cast<DfxBase*>(
          DfxSwap::Create(frame_rate, channels_in, channels_out));
  }

  return nullptr;
}

//
// DfxBase: instance member functions
//
bool DfxBase::GetParameters(fuchsia_audio_dfx_parameters* device_fx_params) {
  device_fx_params->frame_rate = frame_rate_;
  device_fx_params->channels_in = channels_in_;
  device_fx_params->channels_out = channels_out_;

  device_fx_params->signal_latency_frames = frames_latency_;
  device_fx_params->suggested_frames_per_buffer = suggested_buff_frames_;

  return true;
}

}  // namespace audio_dfx_test
}  // namespace media
