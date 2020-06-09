// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Refer to the accompanying README.md file for detailed API documentation
// (functions, structs and constants).

#ifndef LIB_MEDIA_AUDIO_EFFECTS_AUDIO_EFFECTS_H_
#define LIB_MEDIA_AUDIO_EFFECTS_AUDIO_EFFECTS_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <limits>

__BEGIN_CDECLS

typedef void* fuchsia_audio_effects_handle_t;
const fuchsia_audio_effects_handle_t FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE = nullptr;

const uint16_t FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY = std::numeric_limits<uint16_t>::max();
const uint16_t FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN = std::numeric_limits<uint16_t>::max() - 1;
const uint16_t FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX = 256;

const uint32_t FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY = 0;
const uint32_t FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY = 0;

const size_t FUCHSIA_AUDIO_EFFECTS_MAX_NAME_LENGTH = 255;

// Flags that may be passed to fuchsia_audio_effects_module_v1::set_usages
const uint32_t FUCHSIA_AUDIO_EFFECTS_USAGE_NONE           = 0;
const uint32_t FUCHSIA_AUDIO_EFFECTS_USAGE_BACKGROUND     = (1 << 0);
const uint32_t FUCHSIA_AUDIO_EFFECTS_USAGE_MEDIA          = (1 << 1);
const uint32_t FUCHSIA_AUDIO_EFFECTS_USAGE_INTERRUPTION   = (1 << 2);
const uint32_t FUCHSIA_AUDIO_EFFECTS_USAGE_SYSTEM_AGENT   = (1 << 3);
const uint32_t FUCHSIA_AUDIO_EFFECTS_USAGE_COMMUNICATION  = (1 << 4);
// Mask of all valid usage flags. Any bits not covered by this mask may be ignored by
// fuchsia_audio_effects_module_v1::set_usages
const uint32_t FUCHSIA_AUDIO_EFFECTS_USAGE_VALID_MASK     = (1 << 5) - 1;

typedef struct {
  char name[FUCHSIA_AUDIO_EFFECTS_MAX_NAME_LENGTH];
  uint16_t incoming_channels;
  uint16_t outgoing_channels;
} fuchsia_audio_effects_description;

typedef struct {
  uint32_t frame_rate;
  uint16_t channels_in;
  uint16_t channels_out;
  // The block size of the effect. If non-zero, the effect will expect that calls to |process| and
  // |process_inplace| will be called with buffers containing multiples of |block_size_frames|
  // frames.
  //
  // Use |FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY| to indicate any alignment is acceptable.
  uint32_t block_size_frames;

  // When an effect has a positive filter width, it will shift that signal |signal_latency_frames|.
  //
  // In other words, compute frame 'N', 'signal_latency_frames' prior frames are used in the stream
  // as part of that computation.
  uint32_t signal_latency_frames;

  // The maximum number of frames the effect can handle with a single call to |process| or
  // |process_inplace|.
  //
  // Use |FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY| to indicate any frame count is acceptable.
  uint32_t max_frames_per_buffer;

  // When an effect has a negative filter width, it will impact 'ring_out_frames' frames _after_
  // the frame has played.
  //
  // When a stream idles, there will be at least 'ring_out_frames' additional frames of silence
  // provided to the effect to allow this state to 'ring_out'.
  uint32_t ring_out_frames;
} fuchsia_audio_effects_parameters;

typedef struct {
  // A bitmask of the `FUCHSIA_AUDIO_EFFECTS_USAGE_` flags, indicating what usages are represented
  // in the audio frames.
  uint32_t usage_mask;

  // The amount of gain scaling already applied to audio frames before being passed to an effect.
  // Specifically, this represents the _largest_ single gain_dbfs value among this effect's source
  // streams.  It does not account for the accumulated effects of multiple source streams, nor of
  // the actual content of these streams.
  float gain_dbfs;
  // Gain normalized from 0.0 to 1.0 according to the volume curve in use.
  float volume;
} fuchsia_audio_effects_stream_info;

typedef struct {
  // The number of effect types found in the audio_effects lib.  Subsequent APIs that require
  // 'effect_id' will expect values in the range of [ 0, num_effects - 1 ], inclusive.
  uint32_t num_effects;

  // Returns information about this type of effect.
  bool (*get_info)(uint32_t effect_id, fuchsia_audio_effects_description* effect_desc);

  // Returns a 64-bit handle representing an active device effect instance of type ‘effect_id’. In
  // case of failure, FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE is returned. If channels_in ==
  // channels_out, the created effect must process in-place. Conversely, if channels_in !=
  // channels_out, the effect will never process in-place. Also, the system does not yet handle
  // different channel configurations (i.e. it cannot discern between LRCS and quad).
  fuchsia_audio_effects_handle_t (*create_effect)(uint32_t effect_id, uint32_t frame_rate,
                                                  uint16_t channels_in, uint16_t channels_out,
                                                  const char* config, size_t config_length);

  // Updates the effects operational configuration with the new configuration provided by
  // `config`/`config_length`, replacing any and all configuration values provided when the effect
  // was initially created.
  //
  // Returns `true` iff the new configuration was applied successfully.
  bool (*update_effect_configuration)(fuchsia_audio_effects_handle_t effects_handle,
                                      const char* config, size_t config_length);

  // Deletes an active effect.
  bool (*delete_effect)(fuchsia_audio_effects_handle_t effects_handle);

  // Returns the operational parameters for this instance of the device effect. These parameters
  // are invariant for the lifetime of this effect, based on initial values provided when the
  // client created the effect.
  bool (*get_parameters)(fuchsia_audio_effects_handle_t effects_handle,
                         fuchsia_audio_effects_parameters* effect_params);

  // Synchronously process ‘num_frames’ of audio, in-place. The value of 'num_frames' cannot exceed
  // frame_rate: it must be <= 1 second of audio.
  //
  // `process_inplace` requires that `channels_in` == `channels_out`.
  bool (*process_inplace)(fuchsia_audio_effects_handle_t effects_handle, uint32_t num_frames,
                          float* audio_buff_in_out);

  // Synchronously process ‘num_frames’ from audio_buff_in to audio_buff_out. 'num_frames' cannot
  // exceed frame_rate: it must be <= 1 second of audio. The effect implementation is responsible
  // for allocating the buffer returned in |audio_buf_out|. The frames written into |audio_buf_out|
  // must not be modified until a subsequent call to |process|.
  //
  // `process` requires that `channels_in` != `channels_out`.
  bool (*process)(fuchsia_audio_effects_handle_t effects_handle, uint32_t num_frames,
                  const float* audio_buff_in, float** audio_buff_out);

  // Flushes any cached state this effect identified by `effects_handle`.
  bool (*flush)(fuchsia_audio_effects_handle_t effects_handle);

  // Notifies the effect that properties of the audio frames provided to subsequent calls to
  // `process` or `process_inplace` have changed.
  void (*set_stream_info)(fuchsia_audio_effects_handle_t effects_handle,
                          const fuchsia_audio_effects_stream_info* stream_info);
} fuchsia_audio_effects_module_v1;

// Declare an exported module instance from a loadable plugin module:
//
// DECLARE_FUCHSIA_AUDIO_EFFECTS_MODULE_V1 {
//    .num_effects = 2,
//    .get_info = &my_get_info,
//    ... etc ...
// };
#define DECLARE_FUCHSIA_AUDIO_EFFECTS_MODULE_V1 \
  __EXPORT fuchsia_audio_effects_module_v1 fuchsia_audio_effects_module_v1_instance

__END_CDECLS

#endif  // LIB_MEDIA_AUDIO_EFFECTS_AUDIO_EFFECTS_H_
