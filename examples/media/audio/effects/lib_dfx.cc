// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/audio/effects/audio_effects.h>

#include <cmath>

#include <fbl/algorithm.h>

#include "examples/media/audio/effects/dfx_base.h"
#include "examples/media/audio/effects/dfx_delay.h"
#include "examples/media/audio/effects/dfx_rechannel.h"
#include "examples/media/audio/effects/dfx_swap.h"
#include "src/lib/fxl/logging.h"

namespace {

// Returns information about this type of effect
bool fuchsia_audio_dfx_get_info(uint32_t effect_id, fuchsia_audio_effects_description* desc) {
  if (desc == nullptr) {
    return false;
  }

  return media::audio_dfx_test::DfxBase::GetInfo(effect_id, desc);
}

// Returns a fuchsia_audio_effects_handle_t representing active instance of ‘effect_id’ (0 if
// fail). If channels_in==out, effect must process in-place.
fuchsia_audio_effects_handle_t fuchsia_audio_dfx_create(uint32_t effect_id, uint32_t frame_rate,
                                                        uint16_t channels_in, uint16_t channels_out,
                                                        const char* config, size_t config_length) {
  media::audio_dfx_test::DfxBase* effect = media::audio_dfx_test::DfxBase::Create(
      effect_id, frame_rate, channels_in, channels_out, {config, config_length});
  if (effect == nullptr) {
    return FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  }

  return effect;
}

bool fuchsia_audio_dfx_update_configuration(fuchsia_audio_effects_handle_t effects_handle,
                                            const char* config, size_t config_length) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }

  auto effect = reinterpret_cast<media::audio_dfx_test::DfxBase*>(effects_handle);
  return effect->UpdateConfiguration({config, config_length});
}

// Deletes this active effect.
bool fuchsia_audio_dfx_delete(fuchsia_audio_effects_handle_t effects_handle) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(effects_handle);
  delete dfx;

  return true;
}

// Returns various parameters for active effect, including the channelization,
// the number of frames of group delay, and optionally the ideal number of
// frames that the system provides the effect for each call
bool fuchsia_audio_dfx_get_parameters(fuchsia_audio_effects_handle_t effects_handle,
                                      fuchsia_audio_effects_parameters* device_fx_params) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || device_fx_params == nullptr) {
    return false;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(effects_handle);
  return dfx->GetParameters(device_fx_params);
}

// Synchronously processes the buffer of ‘num_frames’ audio data, in-place
bool fuchsia_audio_dfx_process_inplace(fuchsia_audio_effects_handle_t effects_handle,
                                       uint32_t num_frames, float* audio_buff_in_out) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || audio_buff_in_out == nullptr) {
    return false;
  }
  if (num_frames == 0) {
    return true;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(effects_handle);
  return dfx->ProcessInplace(num_frames, audio_buff_in_out);
}

// Synchronously processes ‘num_frames’ from audio_buff_in to audio_buff_out.
bool fuchsia_audio_dfx_process(fuchsia_audio_effects_handle_t effects_handle, uint32_t num_frames,
                               const float* audio_buff_in, float* audio_buff_out) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || audio_buff_in == nullptr ||
      audio_buff_out == nullptr) {
    return false;
  }
  if (num_frames == 0) {
    return true;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(effects_handle);
  return dfx->Process(num_frames, audio_buff_in, audio_buff_out);
}

// Flushes any cached state, but retains settings, on this active effect.
bool fuchsia_audio_dfx_flush(fuchsia_audio_effects_handle_t effects_handle) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }

  auto dfx = reinterpret_cast<media::audio_dfx_test::DfxBase*>(effects_handle);
  return dfx->Flush();
}

}  // namespace

DECLARE_FUCHSIA_AUDIO_EFFECTS_MODULE_V1{
    media::audio_dfx_test::Effect::Count,
    &fuchsia_audio_dfx_get_info,
    &fuchsia_audio_dfx_create,
    &fuchsia_audio_dfx_update_configuration,
    &fuchsia_audio_dfx_delete,
    &fuchsia_audio_dfx_get_parameters,
    &fuchsia_audio_dfx_process_inplace,
    &fuchsia_audio_dfx_process,
    &fuchsia_audio_dfx_flush,
};
