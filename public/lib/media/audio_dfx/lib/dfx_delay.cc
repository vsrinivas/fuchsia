// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/media/audio_dfx/lib/dfx_delay.h"

#include <fbl/algorithm.h>
#include <math.h>

#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/media/audio_dfx/audio_device_fx.h"

namespace media {
namespace audio_dfx_test {

//
// DfxDelay: static member functions
//

// static -- called from static DfxBase::GetInfo; uses DfxDelay classwide consts
bool DfxDelay::GetInfo(fuchsia_audio_dfx_description* dfx_desc) {
  std::strcpy(dfx_desc->name, "Delay effect");
  dfx_desc->num_controls = kNumControls;
  dfx_desc->incoming_channels = kNumChannelsIn;
  dfx_desc->outgoing_channels = kNumChannelsOut;
  return true;
}

// static -- uses DfxDelay classwide consts
bool DfxDelay::GetControlInfo(
    uint16_t control_num,
    fuchsia_audio_dfx_control_description* device_fx_control_desc) {
  if (control_num >= kNumControls) {
    return false;
  }

  std::strcpy(device_fx_control_desc->name, "Delay (in frames)");
  device_fx_control_desc->max_val = static_cast<float>(kMaxDelayFrames);
  device_fx_control_desc->min_val = static_cast<float>(kMinDelayFrames);
  device_fx_control_desc->initial_val = static_cast<float>(kInitialDelayFrames);
  return true;
}

// static -- called from static DfxBase::Create
DfxDelay* DfxDelay::Create(uint32_t frame_rate, uint16_t channels_in,
                           uint16_t channels_out) {
  if (channels_in != channels_out) {
    return nullptr;
  }

  return new DfxDelay(frame_rate, channels_in);
}

//
// DfxDelay: instance member functions
//
DfxDelay::DfxDelay(uint32_t frame_rate, uint16_t channels)
    : DfxBase(Effect::Delay, kNumControls, frame_rate, channels, channels,
              kLatencyFrames, kLatencyFrames) {
  delay_buff_ = std::make_unique<float[]>(kMaxDelayFrames * channels);
  temp_buff_ = std::make_unique<float[]>(kMaxDelayFrames * channels);

  Reset();
}

// Returns FRAMES of delay. We cache SAMPLES, so must convert to frames.
bool DfxDelay::GetControlValue(uint16_t control_num, float* value_out) {
  *value_out = delay_samples_ / channels_in_;
  return true;
}

// This Delay effect chooses to self-flush, upon a change to the delay setting.
bool DfxDelay::SetControlValue(uint16_t control_num, float value) {
  if (value > kMaxDelayFrames || value < kMinDelayFrames) {
    return false;
  }

  uint32_t new_delay_samples = static_cast<uint32_t>(value) * channels_in_;
  if (new_delay_samples != delay_samples_) {
    delay_samples_ = new_delay_samples;
    return Flush();
  }

  return true;
}

// Revert effect instance to a just-initialized state (incl. control settings).
bool DfxDelay::Reset() {
  delay_samples_ = kInitialDelayFrames * channels_in_;

  // Reset should always Flush, once settings are reinitialized.
  return Flush();
}

// TODO(mpuryear) optimize using a circular buffer and std::memmove <cstring>
bool DfxDelay::ProcessInplace(uint32_t num_frames, float* audio_buff) {
  if (delay_samples_ == 0) {
    return true;
  }

  uint32_t num_samples = num_frames * channels_in_;

  // Populate the next buffer of cached delay samples, into a temp buffer. Don't
  // overwrite the real delay buffer because we still need those contents.
  for (uint32_t sample = 0; sample < delay_samples_; ++sample) {
    // If delay is greater than num_frames, then some cache samples are simply
    // previous cache samples that "moved up in line"
    if (sample + num_samples < delay_samples_) {
      temp_buff_[sample] = delay_buff_[sample + num_samples];
    } else {
      // For the rest of the delay buffer, cache newly-submitted samples.
      temp_buff_[sample] = audio_buff[num_samples - delay_samples_ + sample];
    }
  }

  // If num_frames is greater than our delay, then some newly-submitted samples
  // should simply move backward, rather than be moved into our delay cache.
  // Copy back-to-front, so we don't incorrectly overwrite contents.
  for (uint32_t sample = num_samples - 1; sample >= delay_samples_; sample--) {
    audio_buff[sample] = audio_buff[sample - delay_samples_];
  }

  // Populate the front of our audio buffer from previously-cached samples.
  for (uint32_t sample = 0; sample < delay_samples_; ++sample) {
    if (sample < num_samples) {
      audio_buff[sample] = delay_buff_[sample];
    }
    // Also save our temp cache for next time.
    delay_buff_[sample] = temp_buff_[sample];
  }

  return true;
}

// Retain control settings but drop any accumulated state or history.
bool DfxDelay::Flush() {
  for (uint32_t sample = 0; sample < delay_samples_; ++sample) {
    delay_buff_[sample] = 0.0f;
  }

  return true;
}

}  // namespace audio_dfx_test
}  // namespace media
