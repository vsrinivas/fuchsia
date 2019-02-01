// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/media/audio_dfx/audio_device_fx.h"

#include <fbl/algorithm.h>
#include <math.h>

#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_base.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_delay.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_rechannel.h"
#include "garnet/public/lib/media/audio_dfx/lib/dfx_swap.h"

//
// Public API functions
//

bool fuchsia_audio_dfx_get_num_effects(uint32_t* num_effects_out) {
  if (num_effects_out == nullptr) {
    return false;
  }

  return media::audio_dfx_test::DfxBase::GetNumEffects(num_effects_out);
}

// Returns information about this type of effect
bool fuchsia_audio_dfx_get_info(uint32_t effect_id,
                                fuchsia_audio_dfx_description* dfx_desc) {
  if (dfx_desc == nullptr) {
    return false;
  }

  return media::audio_dfx_test::DfxBase::GetInfo(effect_id, dfx_desc);
}

// Returns information about a specific control, on this type of effect
bool fuchsia_audio_dfx_get_control_info(
    uint32_t effect_id, uint16_t control_num,
    fuchsia_audio_dfx_control_description* dfx_control_desc) {
  if (dfx_control_desc == nullptr) {
    return false;
  }

  return media::audio_dfx_test::DfxBase::GetControlInfo(effect_id, control_num,
                                                        dfx_control_desc);
}

// Returns dfx_token representing active instance of ‘effect_id’ (0 if fail).
// If channels_in==out, effect must process in-place.
fx_token_t fuchsia_audio_dfx_create(uint32_t effect_id, uint32_t frame_rate,
                                    uint16_t channels_in,
                                    uint16_t channels_out) {
  media::audio_dfx_test::DfxBase* effect =
      media::audio_dfx_test::DfxBase::Create(effect_id, frame_rate, channels_in,
                                             channels_out);
  if (effect == nullptr) {
    return FUCHSIA_AUDIO_DFX_INVALID_TOKEN;
  }

  return effect;
}

// Deletes this active effect.
bool fuchsia_audio_dfx_delete(fx_token_t dfx_token) {
  if (dfx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return false;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(dfx_token);
  delete dfx;

  return true;
}

// Returns various parameters for active effect, including the channelization,
// the number of frames of group delay, and optionally the ideal number of
// frames that the system provides the effect for each call
bool fuchsia_audio_dfx_get_parameters(
    fx_token_t dfx_token, fuchsia_audio_dfx_parameters* device_fx_params) {
  if (dfx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN ||
      device_fx_params == nullptr) {
    return false;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(dfx_token);
  return dfx->GetParameters(device_fx_params);
}

// Returns the value of the specified control, on this active effect
bool fuchsia_audio_dfx_get_control_value(fx_token_t dfx_token,
                                         uint16_t control_num,
                                         float* value_out) {
  if (dfx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN || value_out == nullptr) {
    return false;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(dfx_token);
  if (control_num >= dfx->num_controls()) {
    return false;
  }

  return dfx->GetControlValue(control_num, value_out);
}

// Sets the value of the specified control, on this active effect
bool fuchsia_audio_dfx_set_control_value(fx_token_t dfx_token,
                                         uint16_t control_num, float value) {
  if (dfx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return false;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(dfx_token);
  if (control_num >= dfx->num_controls()) {
    return false;
  }

  return dfx->SetControlValue(control_num, value);
}

// Returns this active effect to its initial state and settings.
bool fuchsia_audio_dfx_reset(fx_token_t dfx_token) {
  if (dfx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return false;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(dfx_token);
  return dfx->Reset();
}

// Synchronously processes the buffer of ‘num_frames’ audio data, in-place
bool fuchsia_audio_dfx_process_inplace(fx_token_t dfx_token,
                                       uint32_t num_frames,
                                       float* audio_buff_in_out) {
  if (dfx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN ||
      audio_buff_in_out == nullptr) {
    return false;
  }
  if (num_frames == 0) {
    return true;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(dfx_token);
  return dfx->ProcessInplace(num_frames, audio_buff_in_out);
}

// Synchronously processes ‘num_frames’ from audio_buff_in to audio_buff_out.
bool fuchsia_audio_dfx_process(fx_token_t dfx_token, uint32_t num_frames,
                               const float* audio_buff_in,
                               float* audio_buff_out) {
  if (dfx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN ||
      audio_buff_in == nullptr || audio_buff_out == nullptr) {
    return false;
  }
  if (num_frames == 0) {
    return true;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(dfx_token);
  return dfx->Process(num_frames, audio_buff_in, audio_buff_out);
}

// Flushes any cached state, but retains settings, on this active effect.
bool fuchsia_audio_dfx_flush(fx_token_t dfx_token) {
  if (dfx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return false;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(dfx_token);
  return dfx->Flush();
}
