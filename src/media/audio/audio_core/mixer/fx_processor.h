// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_FX_PROCESSOR_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_FX_PROCESSOR_H_

#include <zircon/types.h>

#include <vector>

#include "lib/media/audio_dfx/cpp/audio_device_fx.h"
#include "src/media/audio/audio_core/mixer/fx_loader.h"

namespace media::audio {

// FxProcessor represents a chain of active effect instances, attached to a
// specific device instance. It manages creation and sequencing of instances and
// allows callers to make a single Process or Flush call at media runtime.
//
// Internally, FxProcessor maintains a vector of effect instances. They all
// originate from the same .SO library (hence share a single FxLoader) and run
// at the same frame rate. This class is designed to be used synchronously and
// is not explicitly multi-thread-safe.
class FxProcessor {
 public:
  FxProcessor(FxLoader* loader, uint32_t frame_rate)
      : fx_loader_(loader), frame_rate_(frame_rate) {}
  ~FxProcessor();

  // This maps to the corresponding Create ABI call, inserting it at [position].
  fx_token_t CreateFx(uint32_t effect_id, uint16_t channels_in,
                      uint16_t channels_out, uint8_t position);

  // Returns the number of active instances in the enclosed effect chain.
  uint16_t GetNumFx();

  // Returns the instance at the specified (zero-based) position in the chain.
  fx_token_t GetFxAt(uint16_t position);

  // Move this instance from its current location in the chain to new_position.
  // If the instance moves "leftward", all effects between it and new_position
  // (including the one currently at new_position) will move "rightward" by one.
  // If instance moves "rightward", all effects between it and new_position
  // (including the instance currently at new_position) move "leftward" by one.
  // Either way, afterward this instance resides at [new_position] in the chain.
  zx_status_t ReorderFx(fx_token_t token, uint8_t new_position);

  // This removes instance from the chain and directly calls the DeleteFx ABI.
  zx_status_t DeleteFx(fx_token_t fx_token);

  // This maps directly to the corresponding ABI call, for each instance.
  zx_status_t ProcessInPlace(uint32_t num_frames, float* audio_buff_in_out);

  // This maps directly to the corresponding ABI call, for each instance.
  zx_status_t Flush();

  //
  // Not yet implemented -- these five map directly to corresponding ABI calls.
  //
  // zx_status_t GetParameters(fx_token_t token,fuchsia_audio_dfx_parameters*
  //    params);
  // zx_status_t Process(uint32_t num_frames,const float* audio_buff_in,float*
  //    audio_buff_out);
  // zx_status_t GetControlValue(fx_token_t token, uint16_t ctrl_num, float*
  //    val_out);
  // zx_status_t SetControlValue(fx_token_t token, uint16_t ctrl_num, float
  //    val);
  // zx_status_t Reset(fx_token_t token);

 private:
  // Used internally, this inserts an already-created instance into the chain.
  zx_status_t InsertFx(fx_token_t fx_token, uint8_t position);

  // Used internally, this removes an already-created instance from the chain.
  zx_status_t RemoveFx(fx_token_t fx_token);

  media::audio::FxLoader* fx_loader_;
  uint32_t frame_rate_;

  std::vector<fx_token_t> fx_chain_;
};

}  // namespace media::audio

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_FX_PROCESSOR_H_
