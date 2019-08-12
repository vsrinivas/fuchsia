// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/audio/effects/audio_effects.h>
#include <string.h>

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

  FxPass(uint32_t frame_rate, uint16_t channels) : frame_rate_(frame_rate), channels_(channels) {}
};

// Returns information about this type of effect
bool passthrough_get_info(uint32_t effect_id, fuchsia_audio_effects_description* fx_desc) {
  if (effect_id != 0 || fx_desc == nullptr) {
    return false;
  }

  strlcpy(fx_desc->name, "Pass-thru", sizeof(fx_desc->name));
  fx_desc->incoming_channels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY;
  fx_desc->outgoing_channels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN;
  return true;
}

// Returns a handle representing active instance of ‘effect_id’ (0 if fail).
// If channels_in==out, effect must process in-place.
fuchsia_audio_effects_handle_t passthrough_create(uint32_t effect_id, uint32_t frame_rate,
                                                  uint16_t channels_in, uint16_t channels_out,
                                                  const char* config, size_t config_length) {
  if (effect_id != 0 || channels_in != channels_out ||
      channels_in > FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX) {
    return FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  }

  return reinterpret_cast<fuchsia_audio_effects_handle_t>(new FxPass(frame_rate, channels_in));
}

bool passthrough_update_configuration(fuchsia_audio_effects_handle_t handle, const char* config,
                                      size_t config_length) {
  return (config_length == 0);
}

// Deletes this active effect.
bool passthrough_delete(fuchsia_audio_effects_handle_t handle) {
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }

  auto effect = reinterpret_cast<FxPass*>(handle);
  delete effect;

  return true;
}

// Returns various parameters for this active effect instance: frame rate,
// channelization, frames of group delay, and the ideal number of frames
// provided by the system to the effect with each process[_inplace]() call.
bool passthrough_get_parameters(fuchsia_audio_effects_handle_t handle,
                                fuchsia_audio_effects_parameters* fx_params) {
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || fx_params == nullptr) {
    return false;
  }

  auto effect = reinterpret_cast<FxPass*>(handle);

  fx_params->frame_rate = effect->frame_rate_;
  fx_params->channels_in = effect->channels_;
  fx_params->channels_out = effect->channels_;
  fx_params->signal_latency_frames = 0;
  fx_params->suggested_frames_per_buffer = 0;

  return true;
}

// Synchronously processes the buffer of ‘num_frames’ audio data, in-place.
// This library effect performs no work, so this call immediately returns true.
bool passthrough_process_inplace(fuchsia_audio_effects_handle_t handle, uint32_t,
                                 float* audio_buff_in_out) {
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || audio_buff_in_out == nullptr) {
    return false;
  }

  return true;
}

// Synchronously processes ‘num_frames’ from audio_buff_in to audio_buff_out.
// This library has only in-place effects, so this call always returns false.
bool passthrough_process(fuchsia_audio_effects_handle_t, uint32_t, const float*, float*) {
  return false;
}

// Flushes any cached state, but retains settings, on this active effect.
// This lib has no effects with cached history, so this call performs no work.
bool passthrough_flush(fuchsia_audio_effects_handle_t handle) {
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }

  return true;
}
}  // namespace

DECLARE_FUCHSIA_AUDIO_EFFECTS_MODULE_V1{
    1,
    &passthrough_get_info,
    &passthrough_create,
    &passthrough_update_configuration,
    &passthrough_delete,
    &passthrough_get_parameters,
    &passthrough_process_inplace,
    &passthrough_process,
    &passthrough_flush,

};
