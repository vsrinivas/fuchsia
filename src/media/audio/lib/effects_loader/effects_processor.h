// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_H_

#include <lib/media/audio/effects/audio_effects.h>
#include <zircon/types.h>

#include <string_view>
#include <vector>

#include "src/media/audio/lib/effects_loader/effects_loader.h"

namespace media::audio {

// EffectsProcessor represents a chain of active effect instances, attached to a
// specific device instance. It manages creation and sequencing of instances and
// allows callers to make a single Process or Flush call at media runtime.
//
// Internally, EffectsProcessor maintains a vector of effect instances. They all
// originate from the same .SO library (hence share a single EffectsLoader) and run
// at the same frame rate. This class is designed to be used synchronously and
// is not explicitly multi-thread-safe.
class EffectsProcessor {
 public:
  EffectsProcessor(EffectsLoader* loader, uint32_t frame_rate)
      : effects_loader_(loader), frame_rate_(frame_rate) {}
  ~EffectsProcessor();

  // This maps to the corresponding Create ABI call, inserting it at [position].
  fuchsia_audio_effects_handle_t CreateFx(uint32_t effect_id, uint16_t channels_in,
                                          uint16_t channels_out, uint8_t position,
                                          std::string_view config);

  // Returns the number of active instances in the enclosed effect chain.
  uint16_t GetNumFx();

  // Returns the instance at the specified (zero-based) position in the chain.
  fuchsia_audio_effects_handle_t GetFxAt(uint16_t position);

  // Move this instance from its current location in the chain to new_position.
  // If the instance moves "leftward", all effects between it and new_position
  // (including the one currently at new_position) will move "rightward" by one.
  // If instance moves "rightward", all effects between it and new_position
  // (including the instance currently at new_position) move "leftward" by one.
  // Either way, afterward this instance resides at [new_position] in the chain.
  zx_status_t ReorderFx(fuchsia_audio_effects_handle_t handle, uint8_t new_position);

  // This removes instance from the chain and directly calls the DeleteFx ABI.
  zx_status_t DeleteFx(fuchsia_audio_effects_handle_t handle);

  // This maps directly to the corresponding ABI call, for each instance.
  zx_status_t ProcessInPlace(uint32_t num_frames, float* audio_buff_in_out);

  // This maps directly to the corresponding ABI call, for each instance.
  zx_status_t Flush();

  //
  // Not yet implemented -- these map directly to corresponding ABI calls.
  //
  // zx_status_t GetParameters(fuchsia_audio_effects_handle_t
  // handle,fuchsia_audio_effects_parameters*
  //    params);
  // zx_status_t Process(uint32_t num_frames,const float* audio_buff_in,float*
  //    audio_buff_out);

 private:
  // Used internally, this inserts an already-created instance into the chain.
  zx_status_t InsertFx(fuchsia_audio_effects_handle_t handle, uint8_t position);

  // Used internally, this removes an already-created instance from the chain.
  zx_status_t RemoveFx(fuchsia_audio_effects_handle_t handle);

  media::audio::EffectsLoader* effects_loader_;
  uint32_t frame_rate_;

  std::vector<fuchsia_audio_effects_handle_t> fx_chain_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_H_
