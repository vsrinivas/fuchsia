// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_EFFECTS_TEST_EFFECTS_TEST_EFFECTS_H_
#define SRC_MEDIA_AUDIO_EFFECTS_TEST_EFFECTS_TEST_EFFECTS_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

enum EffectAction {
  // For each channel and sample, assign the output to a fixed value.
  TEST_EFFECTS_ACTION_ASSIGN = 0,

  // For each channel and sample, assign the output to the input value plus a fixed value.
  //
  // Requires in_channels == out_channels
  TEST_EFFECTS_ACTION_ADD = 1,
};

typedef struct {
  fuchsia_audio_effects_description description;
  uint32_t block_size_frames;
  uint32_t max_batch_size;
  EffectAction action;
  float value;
} test_effect_spec;

typedef struct {
  // The most recent configuration string set for this effect. This pointer will remain valid until
  // the next call to fuchsia_audio_effects_module_v1::update_effect_configuration.
  const char* config;
  size_t config_length;

  // The effect_id used to create this instance.
  uint32_t effect_id;

  // The number of times this effect has been flushed.
  size_t flush_count;
} test_effects_inspect_state;

// |test_effects_module_ext| is an extension interface that can be used to configure the behavior
// of the |test_effects| module. By interacting with this interface, tests can configure the
// behavior of this effect module.
typedef struct {
  // Creates a new effect for the library. Must be called while the number of active effect
  // instances is zero.
  zx_status_t (*add_effect)(test_effect_spec effect);

  // Removes all effects. Must be called while the number of active effect instances is zero.
  zx_status_t (*clear_effects)();

  // Returns the number of active effect instances owned by this module.
  uint32_t (*num_instances)();

  // Provides detailed information about a single effect instance.
  zx_status_t (*inspect_instance)(fuchsia_audio_effects_handle_t effects_handle,
                                  test_effects_inspect_state* out);
} test_effects_module_ext;

#define DECLARE_TEST_EFFECTS_EXT __EXPORT test_effects_module_ext test_effects_module_ext_instance

__END_CDECLS

#endif  // SRC_MEDIA_AUDIO_EFFECTS_TEST_EFFECTS_TEST_EFFECTS_H_
