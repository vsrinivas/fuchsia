// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_processor.h"

#include "src/lib/fxl/logging.h"

namespace media::audio {

// If any instances remain, remove and delete them before we leave.
EffectsProcessor::~EffectsProcessor() {
  while (!fx_chain_.empty()) {
    DeleteFx(GetFxAt(0));
  }

  // release any reference on effects_loader_
}

// Create and insert an effect instance, at the specified position.
// If position is out-of-range, return an error (don't clamp).
fuchsia_audio_effects_handle_t EffectsProcessor::CreateFx(uint32_t effect_id, uint16_t channels_in,
                                                          uint16_t channels_out, uint8_t position,
                                                          std::string_view config) {
  fuchsia_audio_effects_handle_t handle =
      effects_loader_->CreateFx(effect_id, frame_rate_, channels_in, channels_out, config);

  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return handle;
  }

  // If we successfully create but can't insert, delete before returning error.
  if (InsertFx(handle, position) != ZX_OK) {
    effects_loader_->DeleteFx(handle);
    return FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  }
  return handle;
}

// Return the number of active instances in this chain.
uint16_t EffectsProcessor::GetNumFx() { return fx_chain_.size(); }

// If position is out-of-range, return an error (don't clamp).
fuchsia_audio_effects_handle_t EffectsProcessor::GetFxAt(uint16_t position) {
  if (position >= fx_chain_.size()) {
    return FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  }

  return fx_chain_[position];
}

// Move the specified instance to a new position in the FX chain.
// If position is out-of-range, return an error (don't clamp).
zx_status_t EffectsProcessor::ReorderFx(fuchsia_audio_effects_handle_t handle,
                                        uint8_t new_position) {
  if (new_position >= fx_chain_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (RemoveFx(handle) != ZX_OK) {
    return ZX_ERR_NOT_FOUND;
  }

  return InsertFx(handle, new_position);
}

// Remove and delete the specified instance.
zx_status_t EffectsProcessor::DeleteFx(fuchsia_audio_effects_handle_t handle) {
  if (effects_loader_ == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t ret_val = RemoveFx(handle);
  if (ret_val == ZX_OK) {
    ret_val = effects_loader_->DeleteFx(handle);
  }

  return ret_val;
}

// For this FX chain, call each instance's FxProcessInPlace() in sequence.
// Per spec, fail if audio_buff_in_out is nullptr (even if num_frames is 0).
// Also, if any instance fails Process, exit without calling the others.
// TODO(mpuryear): Should we still call the other instances, if one fails?
zx_status_t EffectsProcessor::ProcessInPlace(uint32_t num_frames, float* audio_buff_in_out) {
  if (audio_buff_in_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (num_frames == 0) {
    return ZX_OK;
  }

  for (auto handle : fx_chain_) {
    if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
      return ZX_ERR_INTERNAL;
    }

    zx_status_t ret_val = effects_loader_->FxProcessInPlace(handle, num_frames, audio_buff_in_out);
    if (ret_val != ZX_OK) {
      return ret_val;
    }
  }

  return ZX_OK;
}

// For this FX chain, call each instance's FxFlush() in sequence.
// If any instance fails, exit without calling the others.
// TODO(mpuryear): Because Flush is a cleanup, do we Flush ALL even on error?
zx_status_t EffectsProcessor::Flush() {
  for (auto handle : fx_chain_) {
    if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
      return ZX_ERR_INTERNAL;
    }

    zx_status_t ret_val = effects_loader_->FxFlush(handle);
    if (ret_val != ZX_OK) {
      return ret_val;
    }
  }

  return ZX_OK;
}

//
// Private internal methods
//

// Insert an already-created effect instance at the specified position.
// If position is out-of-range, return an error (don't clamp).
zx_status_t EffectsProcessor::InsertFx(fuchsia_audio_effects_handle_t handle, uint8_t position) {
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (position > fx_chain_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fx_chain_.insert(fx_chain_.begin() + position, handle);
  return ZX_OK;
}

// Remove an existing effect instance from the FX chain.
zx_status_t EffectsProcessor::RemoveFx(fuchsia_audio_effects_handle_t handle) {
  auto iter = std::find(fx_chain_.begin(), fx_chain_.end(), handle);
  if (iter == fx_chain_.end()) {
    return ZX_ERR_NOT_FOUND;
  }

  fx_chain_.erase(iter);
  return ZX_OK;
}

}  // namespace media::audio
