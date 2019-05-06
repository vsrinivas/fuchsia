// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/fx_processor.h"

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/mixer/fx_loader.h"

namespace media::audio {

// If any instances remain, remove and delete them before we leave.
FxProcessor::~FxProcessor() {
  while (!fx_chain_.empty()) {
    DeleteFx(GetFxAt(0));
  }

  // release any reference on fx_loader_
}

// Create and insert an effect instance, at the specified position.
// If position is out-of-range, return an error (don't clamp).
fx_token_t FxProcessor::CreateFx(uint32_t effect_id, uint16_t channels_in,
                                 uint16_t channels_out, uint8_t position) {
  fx_token_t fx_token =
      fx_loader_->CreateFx(effect_id, frame_rate_, channels_in, channels_out);

  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return fx_token;
  }

  // If we successfully create but can't insert, delete before returning error.
  if (InsertFx(fx_token, position) != ZX_OK) {
    fx_loader_->DeleteFx(fx_token);
    return FUCHSIA_AUDIO_DFX_INVALID_TOKEN;
  }
  return fx_token;
}

// Return the number of active instances in this chain.
uint16_t FxProcessor::GetNumFx() { return fx_chain_.size(); }

// If position is out-of-range, return an error (don't clamp).
fx_token_t FxProcessor::GetFxAt(uint16_t position) {
  if (position >= fx_chain_.size()) {
    return FUCHSIA_AUDIO_DFX_INVALID_TOKEN;
  }

  return fx_chain_[position];
}

// Move the specified instance to a new position in the FX chain.
// If position is out-of-range, return an error (don't clamp).
zx_status_t FxProcessor::ReorderFx(fx_token_t fx_token, uint8_t new_position) {
  if (new_position >= fx_chain_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (RemoveFx(fx_token) != ZX_OK) {
    return ZX_ERR_NOT_FOUND;
  }

  return InsertFx(fx_token, new_position);
}

// Remove and delete the specified instance.
zx_status_t FxProcessor::DeleteFx(fx_token_t fx_token) {
  if (fx_loader_ == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t ret_val = RemoveFx(fx_token);
  if (ret_val == ZX_OK) {
    ret_val = fx_loader_->DeleteFx(fx_token);
  }

  return ret_val;
}

// For this FX chain, call each instance's FxProcessInPlace() in sequence.
// Per spec, fail if audio_buff_in_out is nullptr (even if num_frames is 0).
// Also, if any instance fails Process, exit without calling the others.
// TODO(mpuryear): Should we still call the other instances, if one fails?
zx_status_t FxProcessor::ProcessInPlace(uint32_t num_frames,
                                        float* audio_buff_in_out) {
  if (audio_buff_in_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (num_frames == 0) {
    return ZX_OK;
  }

  for (auto fx_token : fx_chain_) {
    if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
      return ZX_ERR_INTERNAL;
    }

    zx_status_t ret_val =
        fx_loader_->FxProcessInPlace(fx_token, num_frames, audio_buff_in_out);
    if (ret_val != ZX_OK) {
      return ret_val;
    }
  }

  return ZX_OK;
}

// For this FX chain, call each instance's FxFlush() in sequence.
// If any instance fails, exit without calling the others.
// TODO(mpuryear): Because Flush is a cleanup, do we Flush ALL even on error?
zx_status_t FxProcessor::Flush() {
  for (auto fx_token : fx_chain_) {
    if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
      return ZX_ERR_INTERNAL;
    }

    zx_status_t ret_val = fx_loader_->FxFlush(fx_token);
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
zx_status_t FxProcessor::InsertFx(fx_token_t fx_token, uint8_t position) {
  if (fx_token == FUCHSIA_AUDIO_DFX_INVALID_TOKEN) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (position > fx_chain_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fx_chain_.insert(fx_chain_.begin() + position, fx_token);
  return ZX_OK;
}

// Remove an existing effect instance from the FX chain.
zx_status_t FxProcessor::RemoveFx(fx_token_t fx_token) {
  auto iter = std::find(fx_chain_.begin(), fx_chain_.end(), fx_token);
  if (iter == fx_chain_.end()) {
    return ZX_ERR_NOT_FOUND;
  }

  fx_chain_.erase(iter);
  return ZX_OK;
}

}  // namespace media::audio
