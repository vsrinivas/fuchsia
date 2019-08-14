// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_processor.h"

#include "src/lib/fxl/logging.h"

namespace media::audio {

// Insert an effect instance at the end of the chain.
void EffectsProcessor::AddEffect(Effect e) {
  FXL_DCHECK(e);
  effects_chain_.emplace_back(std::move(e));
}

// Aborts if position is out-of-range.
const Effect& EffectsProcessor::GetEffectAt(size_t position) const {
  FXL_DCHECK(position < effects_chain_.size());
  return effects_chain_[position];
}

// For this FX chain, call each instance's FxProcessInPlace() in sequence.
// Per spec, fail if audio_buff_in_out is nullptr (even if num_frames is 0).
// Also, if any instance fails Process, exit without calling the others.
// TODO(mpuryear): Should we still call the other instances, if one fails?
zx_status_t EffectsProcessor::ProcessInPlace(uint32_t num_frames, float* audio_buff_in_out) const {
  if (audio_buff_in_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (num_frames == 0) {
    return ZX_OK;
  }

  for (const auto& effect : effects_chain_) {
    if (!effect) {
      return ZX_ERR_INTERNAL;
    }

    zx_status_t ret_val = effect.ProcessInPlace(num_frames, audio_buff_in_out);
    if (ret_val != ZX_OK) {
      return ret_val;
    }
  }

  return ZX_OK;
}

// For this Effect chain, call each instance's Flush() in sequence. If any instance fails, continue
// Flushing the remaining Effects but only the first error will be reported.
//
// Return ZX_OK iff all Effects are successfully flushed.
zx_status_t EffectsProcessor::Flush() const {
  zx_status_t result = ZX_OK;
  for (const auto& effect : effects_chain_) {
    if (!effect) {
      return ZX_ERR_INTERNAL;
    }

    zx_status_t ret_val = effect.Flush();
    if (ret_val != ZX_OK && result == ZX_OK) {
      result = ret_val;
    }
  }

  return result;
}

}  // namespace media::audio
