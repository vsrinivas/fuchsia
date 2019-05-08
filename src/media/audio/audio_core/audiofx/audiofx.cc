// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdk/lib/media/audio_dfx/cpp/audio_device_fx.h"
#include "src/lib/fxl/logging.h"

//
// This minimal library has such limited functionality that we implement it
// right here in the library dispatcher .cc file without additional FX .h or .cc
// files. Even the entities that represent effects are structs, not objects.
//
namespace {
// FxPass: in-place effect with no controls, channel restrictions or latency.
struct FxPass {
  uint32_t frame_rate_;
  uint16_t channels_;

  FxPass(uint32_t frame_rate, uint16_t channels)
      : frame_rate_(frame_rate), channels_(channels) {}
};
}  // namespace

//
// Public API functions
//
bool fuchsia_audio_dfx_get_num_effects(uint32_t* num_fx_out) {
  if (num_fx_out == nullptr) {
    return false;
  }

  *num_fx_out = 1;
  return true;
}

// Returns information about this type of effect
bool fuchsia_audio_dfx_get_info(uint32_t effect_id,
                                fuchsia_audio_dfx_description* fx_desc) {
  if (effect_id != 0 || fx_desc == nullptr) {
    return false;
  }

  strlcpy(fx_desc->name, "Pass-thru", sizeof(fx_desc->name));
  fx_desc->num_controls = 0;
  fx_desc->incoming_channels = FUCHSIA_AUDIO_DFX_CHANNELS_ANY;
  fx_desc->outgoing_channels = FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN;
  return true;
}

// Returns information about a specific control, on this type of effect.
// This library has no effects with controls, so this call always returns false.
bool fuchsia_audio_dfx_get_control_info(
    uint32_t, uint16_t, fuchsia_audio_dfx_control_description*) {
  return false;
}

// Returns fx_token representing active instance of ‘effect_id’ (0 if fail).
// If channels_in==out, effect must process in-place.
fx_token_t fuchsia_audio_dfx_create(uint32_t effect_id, uint32_t frame_rate,
                                    uint16_t channels_in,
                                    uint16_t channels_out) {
  if (effect_id != 0 || channels_in != channels_out ||
      channels_in > FUCHSIA_AUDIO_DFX_CHANNELS_MAX) {
    return FUCHSIA_AUDIO_DFX_INVALID_TOKEN;
  }

  return reinterpret_cast<fx_token_t>(new FxPass(frame_rate, channels_in));
}

// Deletes this active effect.
bool fuchsia_audio_dfx_delete(fx_token_t fx_token) {
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return false;
  }

  auto effect = reinterpret_cast<FxPass*>(fx_token);
  delete effect;

  return true;
}

// Returns various parameters for this active effect instance: frame rate,
// channelization, frames of group delay, and the ideal number of frames
// provided by the system to the effect with each process[_inplace]() call.
bool fuchsia_audio_dfx_get_parameters(fx_token_t fx_token,
                                      fuchsia_audio_dfx_parameters* fx_params) {
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN || fx_params == nullptr) {
    return false;
  }

  auto effect = reinterpret_cast<FxPass*>(fx_token);

  fx_params->frame_rate = effect->frame_rate_;
  fx_params->channels_in = effect->channels_;
  fx_params->channels_out = effect->channels_;
  fx_params->signal_latency_frames = 0;
  fx_params->suggested_frames_per_buffer = 0;

  return true;
}

// Returns the value of the specified control, on this active effect instance.
// This library has no effects with controls, so this call always returns false.
bool fuchsia_audio_dfx_get_control_value(fx_token_t, uint16_t, float*) {
  return false;
}

// Sets the value of the specified control, on this active effect instance.
// This library has no effects with controls, so this call always returns false.
bool fuchsia_audio_dfx_set_control_value(fx_token_t, uint16_t, float) {
  return false;
}

// Returns this active effect instance to its initial state and settings.
// This library has no effects with controls, so this call performs no work.
bool fuchsia_audio_dfx_reset(fx_token_t fx_token) {
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return false;
  }

  return true;
}

// Synchronously processes the buffer of ‘num_frames’ audio data, in-place.
// This library effect performs no work, so this call immediately returns true.
bool fuchsia_audio_dfx_process_inplace(fx_token_t fx_token, uint32_t,
                                       float* audio_buff_in_out) {
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN ||
      audio_buff_in_out == nullptr) {
    return false;
  }

  return true;
}

// Synchronously processes ‘num_frames’ from audio_buff_in to audio_buff_out.
// This library has only in-place effects, so this call always returns false.
bool fuchsia_audio_dfx_process(fx_token_t, uint32_t, const float*, float*) {
  return false;
}

// Flushes any cached state, but retains settings, on this active effect.
// This lib has no effects with cached history, so this call performs no work.
bool fuchsia_audio_dfx_flush(fx_token_t fx_token) {
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return false;
  }

  return true;
}
