// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_processor.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <numeric>

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

  if (channels_out_ && params.channels_in != channels_out_) {
    FX_LOGS(ERROR) << "Can't add effect; channelization mismatch. Requires " << channels_out_
                   << ", but expects " << params.channels_in;
    return ZX_ERR_INVALID_ARGS;
  }
  if (channels_in_ == 0) {
    channels_in_ = params.channels_in;
  }
  channels_out_ = params.channels_out;

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

  delay_frames_ += params.signal_latency_frames;
  ring_out_frames_ += params.ring_out_frames;
  effects_chain_.emplace_back(std::move(e));
  effects_parameters_.emplace_back(std::move(params));
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

zx_status_t EffectsProcessor::Process(uint32_t num_frames, float* audio_buff_in,
                                      float** audio_buff_out) const {
  TRACE_DURATION("audio", "EffectsProcessor::Process", "num_frames", num_frames, "num_effects",
                 effects_chain_.size());
  if (audio_buff_in == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (num_frames == 0) {
    return ZX_OK;
  }

  uint32_t channels_in = channels_in_;
  float* input = audio_buff_in;
  for (size_t i = 0; i < effects_chain_.size(); ++i) {
    const auto& effect = effects_chain_[i];
    const auto& parameters = effects_parameters_[i];
    if (!effect) {
      return ZX_ERR_INTERNAL;
    }

    zx_status_t ret_val;
    FX_DCHECK(parameters.channels_in == channels_in);
    if (parameters.channels_out == channels_in) {
      ret_val = effect.ProcessInPlace(num_frames, input);
    } else {
      ret_val = effect.Process(num_frames, input, &input);
      channels_in = parameters.channels_out;
    }
    if (ret_val != ZX_OK) {
      return ret_val;
    }
  }

  *audio_buff_out = input;
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

void EffectsProcessor::SetStreamInfo(const fuchsia_audio_effects_stream_info& stream_info) const {
  TRACE_DURATION("audio", "EffectsProcessor::SetStreamInfo");
  for (const auto& effect : effects_chain_) {
    if (!effect) {
      continue;
    }

    effect.SetStreamInfo(stream_info);
  }
}

}  // namespace media::audio
