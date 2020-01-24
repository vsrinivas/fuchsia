// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_processor.h"

#include <lib/trace/event.h>

#include <numeric>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {
namespace {

uint32_t ComputeMinBlockSize(uint32_t a, uint32_t b) { return std::lcm(a, b); }

uint32_t ComputeMaxFramesPerBuffer(uint32_t max_frames_per_buffer, uint32_t block_size) {
  // Align max batch size to work with our block size.
  return max_frames_per_buffer - (max_frames_per_buffer % block_size);
}

}  // namespace

// Insert an effect instance at the end of the chain.
zx_status_t EffectsProcessor::AddEffect(Effect e) {
  TRACE_DURATION("audio", "EffectsProcessor::AddEffect");
  FX_DCHECK(e);

  fuchsia_audio_effects_parameters params;
  zx_status_t status = e.GetParameters(&params);
  if (status != ZX_OK) {
    return status;
  }

  // For now we only support in-place processors.
  if (params.channels_in != params.channels_out) {
    FX_LOGS(ERROR) << "Can't add effect; only in-place effects are currently supported.";
    return ZX_ERR_INVALID_ARGS;
  }

  if (params.block_size_frames != FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY &&
      params.block_size_frames != block_size_) {
    block_size_ = ComputeMinBlockSize(block_size_, params.block_size_frames);
    if (max_batch_size_ != 0) {
      // Recompute our max batch size to be block aligned.
      max_batch_size_ = ComputeMaxFramesPerBuffer(max_batch_size_, block_size_);
    }
  }

  if (params.max_frames_per_buffer != FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY &&
      (max_batch_size_ == 0 || params.max_frames_per_buffer < max_batch_size_)) {
    max_batch_size_ = ComputeMaxFramesPerBuffer(params.max_frames_per_buffer, block_size_);
  }

  if (effects_chain_.empty()) {
    // This is the first effect; the processors input channels will be whatever this effect
    // accepts.
    channels_in_ = params.channels_in;
  } else if (params.channels_in != channels_out_) {
    // We have existing effects and this effect excepts different channelization than what we're
    // currently producing.
    FX_LOGS(ERROR) << "Can't add effect; needs " << params.channels_in << " channels but have "
                   << channels_out_ << " channels";
    return ZX_ERR_INVALID_ARGS;
  }

  channels_out_ = params.channels_out;
  effects_chain_.emplace_back(std::move(e));
  return ZX_OK;
}

// Aborts if position is out-of-range.
const Effect& EffectsProcessor::GetEffectAt(size_t position) const {
  TRACE_DURATION("audio", "EffectsProcessor::GetEffectAt", "position", position);
  FX_DCHECK(position < effects_chain_.size());
  return effects_chain_[position];
}

// For this FX chain, call each instance's FxProcessInPlace() in sequence.
// Per spec, fail if audio_buff_in_out is nullptr (even if num_frames is 0).
// Also, if any instance fails Process, exit without calling the others.
// TODO(mpuryear): Should we still call the other instances, if one fails?
zx_status_t EffectsProcessor::ProcessInPlace(uint32_t num_frames, float* audio_buff_in_out) const {
  TRACE_DURATION("audio", "EffectsProcessor::ProcessInPlace", "num_frames", num_frames,
                 "num_effects", effects_chain_.size());
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
  TRACE_DURATION("audio", "EffectsProcessor::Flush");
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
