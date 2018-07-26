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
  // This buff must accomodate our maximum delay, plus the largest 'num_frames'
  // required by process_inplace -- which can be as large as frame_rate.
  delay_buff_ =
      std::make_unique<float[]>((kMaxDelayFrames + frame_rate) * channels);

  Reset();
}

// Returns FRAMES of delay. We cache SAMPLES for convenience, so convert back.
bool DfxDelay::GetControlValue(uint16_t control_num, float* value_out) {
  *value_out = delay_samples_ / channels_in_;
  return true;
}

// This effect chooses to self-flush, upon any change to our delay setting.
bool DfxDelay::SetControlValue(uint16_t control_num, float value) {
  if (control_num >= kNumControls || value > kMaxDelayFrames ||
      value < kMinDelayFrames) {
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
// Reset should always Flush, if relevant settings are changed.
bool DfxDelay::Reset() {
  delay_samples_ = kInitialDelayFrames * channels_in_;

  return Flush();
}

// Delay the incoming stream by the number of frames specified in control 0.
//
// TODO: with circular buffer, optimize 2N+D to N+min(N,D), where N=num_frames;
// D=delay. Suggested algorithm: 1.copy min(N,D) from aud_buf to cache;
// 2.shift max(N-D,0) within aud_buf; 3.copy min(N,D) from cache to aud_buf.
bool DfxDelay::ProcessInplace(uint32_t num_frames, float* audio_buff) {
  if (delay_samples_ > 0) {
    uint32_t audio_buff_bytes = num_frames * channels_in_ * sizeof(float);

    // DfxDelay maintains a "delay cache" containing the next samples to emit.
    // 1) Copy all samples from audio_buff to delay cache (after previous ones).
    ::memcpy(delay_buff_.get() + delay_samples_, audio_buff, audio_buff_bytes);
    // 2) Fill audio_buff, from front of the delay cache.
    ::memcpy(audio_buff, delay_buff_.get(), audio_buff_bytes);
    // 3) Shift the remaining cached samples to the front of the delay cache.
    ::memmove(delay_buff_.get(), delay_buff_.get() + num_frames * channels_in_,
              delay_samples_ * sizeof(float));
  }

  return true;
}
// Retain control settings but drop any accumulated state or history.
bool DfxDelay::Flush() {
  ::memset(delay_buff_.get(), 0, delay_samples_ * sizeof(float));

  return true;
}

}  // namespace audio_dfx_test
}  // namespace media
