// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef LIB_AUDIO_DEVICE_FX_H_
#define LIB_AUDIO_DEVICE_FX_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <limits>

__BEGIN_CDECLS

const size_t FUCHSIA_AUDIO_DFX_MAX_NAME_LENGTH = 255;
const uint64_t FUCHSIA_AUDIO_DFX_INVALID_TOKEN = 0;

const uint16_t FUCHSIA_AUDIO_DFX_CHANNELS_ANY =
    std::numeric_limits<uint16_t>::max();
const uint16_t FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN =
    std::numeric_limits<uint16_t>::max() - 1;
const uint16_t FUCHSIA_AUDIO_DFX_CHANNELS_MAX =
    std::numeric_limits<int16_t>::max();

typedef struct {
  char name[FUCHSIA_AUDIO_DFX_MAX_NAME_LENGTH];
  uint16_t num_controls;
  uint16_t incoming_channels;
  uint16_t outgoing_channels;
} fuchsia_audio_dfx_description;

typedef struct {
  char name[FUCHSIA_AUDIO_DFX_MAX_NAME_LENGTH];
  float max_val;
  float min_val;
  float initial_val;
} fuchsia_audio_dfx_control_description;

typedef struct {
  uint32_t frame_rate;
  uint16_t channels_in;
  uint16_t channels_out;
  uint32_t signal_latency_frames;
  uint32_t suggested_frames_per_buffer;
} fuchsia_audio_dfx_parameters;

// Returns the number of effect types found in the audio_dfx lib.  Subsequent
// APIs that require 'effect_id' will expect values in the range of
// [ 0, *num_effects_out - 1 ], inclusive.
__EXPORT bool fuchsia_audio_dfx_get_num_effects(uint32_t* num_effects_out);

// Returns information about this type of effect, including number of controls.
// Subsequent APIs that require 'control_num' will expect values in the range of
// [ 0 , device_fx_desc->num_controls - 1 ], inclusive.
__EXPORT bool fuchsia_audio_dfx_get_info(
    uint32_t effect_id, fuchsia_audio_dfx_description* device_fx_desc);

// Returns information about a specific control, on this type of effect.
__EXPORT bool fuchsia_audio_dfx_get_control_info(
    uint32_t effect_id, uint16_t control_num,
    fuchsia_audio_dfx_control_description* device_fx_control_desc);

// Returns a 64-bit token representing an active device effect instance of type
// ‘effect_id’. In case of failure, FUCHSIA_AUDIO_DFX_INVALID_TOKEN is returned.
// If channels_in == channels_out, the created effect must process in-place.
// As stated in media_types.fidl, currently the maximum supported number of
// channels is eight. Also, the system does not yet handle different channel
// configurations (i.e. it cannot discern between LRCS and quad).
// TODO(mpuryear): Incorporate basic channel configuration (initially via simple
// enum) to the driver, DFX, mixer, renderer and capturer interfaces.
// TODO(mpuryear): Enable the mixer to mix between basic channel configurations.
__EXPORT uint64_t fuchsia_audio_dfx_create(uint32_t effect_id,
                                           uint32_t frame_rate,
                                           uint16_t channels_in,
                                           uint16_t channels_out);

// Deletes this active effect.
__EXPORT bool fuchsia_audio_dfx_delete(uint64_t dfx_token);

// Returns the operational parameters for this instance of the device effect.
// These parameters are invariant for the lifetime of this effect, based on
// initial values provided when the client created the effect.
__EXPORT bool fuchsia_audio_dfx_get_parameters(
    uint64_t dfx_token, fuchsia_audio_dfx_parameters* device_fx_params);

// Returns the value of the specified control, on this active effect.
__EXPORT bool fuchsia_audio_dfx_get_control_value(uint64_t dfx_token,
                                                  uint16_t control_num,
                                                  float* value_out);

// Sets the value of the specified control, on this active effect.
__EXPORT bool fuchsia_audio_dfx_set_control_value(uint64_t dfx_token,
                                                  uint16_t control_num,
                                                  float value);

// Returns this active effect to its initial state and settings.
__EXPORT bool fuchsia_audio_dfx_reset(uint64_t dfx_token);

// Synchronously processes the buffer of ‘num_frames’ audio data, in-place.
__EXPORT bool fuchsia_audio_dfx_process_inplace(uint64_t dfx_token,
                                                uint32_t num_frames,
                                                float* audio_buff_in_out);

// Synchronously processes ‘num_frames’ from audio_buff_in to audio_buff_out.
__EXPORT bool fuchsia_audio_dfx_process(uint64_t dfx_token, uint32_t num_frames,
                                        const float* audio_buff_in,
                                        float* audio_buff_out);

// Flushes any cached state, but retains settings, on this active effect.
__EXPORT bool fuchsia_audio_dfx_flush(uint64_t dfx_token);

__END_CDECLS

#endif  // LIB_AUDIO_DEVICE_FX_H_
