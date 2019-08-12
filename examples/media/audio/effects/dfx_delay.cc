// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/media/audio/effects/dfx_delay.h"

#include <cmath>
#include <optional>

#include <fbl/algorithm.h>
#include <rapidjson/document.h>

#include "src/lib/fxl/logging.h"

namespace media::audio_dfx_test {
namespace {
struct DelayConfig {
  uint32_t delay_frames;
};

std::optional<DelayConfig> ParseConfig(std::string_view config_json) {
  rapidjson::Document document;
  document.Parse(config_json.data(), config_json.size());
  if (!document.IsObject()) {
    return {};
  }
  auto it = document.FindMember("delay_frames");
  if (it == document.MemberEnd()) {
    return {};
  }
  if (!it->value.IsUint()) {
    return {};
  }
  auto delay_frames = it->value.GetUint();
  if (delay_frames > DfxDelay::kMaxDelayFrames || delay_frames < DfxDelay::kMinDelayFrames) {
    return {};
  }

  return {{delay_frames}};
}

constexpr uint32_t ComputeDelaySamples(uint16_t channels_in, uint16_t delay_frames) {
  return channels_in * delay_frames;
}

}  // namespace

//
// DfxDelay: static member functions
//

// static -- called from static DfxBase::GetInfo; uses DfxDelay classwide consts
bool DfxDelay::GetInfo(fuchsia_audio_effects_description* dfx_desc) {
  strlcpy(dfx_desc->name, "Delay effect", sizeof(dfx_desc->name));
  dfx_desc->incoming_channels = kNumChannelsIn;
  dfx_desc->outgoing_channels = kNumChannelsOut;
  return true;
}

// static -- called from static DfxBase::Create
DfxDelay* DfxDelay::Create(uint32_t frame_rate, uint16_t channels_in, uint16_t channels_out,
                           std::string_view config_json) {
  if (channels_in != channels_out) {
    return nullptr;
  }

  auto config = ParseConfig(config_json);
  if (!config) {
    return nullptr;
  }

  return new DfxDelay(frame_rate, channels_in, config->delay_frames);
}

//
// DfxDelay: instance member functions
//
DfxDelay::DfxDelay(uint32_t frame_rate, uint16_t channels, uint32_t delay_frames)
    : DfxBase(Effect::Delay, frame_rate, channels, channels, kLatencyFrames, kLatencyFrames) {
  // This buff must accommodate our maximum delay, plus the largest 'num_frames'
  // required by process_inplace -- which can be as large as frame_rate.
  delay_buff_ = std::make_unique<float[]>((kMaxDelayFrames + frame_rate) * channels);

  delay_samples_ = ComputeDelaySamples(channels_in_, delay_frames);
  ::memset(delay_buff_.get(), 0, delay_samples_ * sizeof(float));
}

bool DfxDelay::UpdateConfiguration(std::string_view config_json) {
  auto config = ParseConfig(config_json);
  if (!config) {
    return false;
  }

  uint32_t new_delay_samples = ComputeDelaySamples(channels_in_, config->delay_frames);
  if (new_delay_samples != delay_samples_) {
    delay_samples_ = new_delay_samples;
    return Flush();
  }
  return true;
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

}  // namespace media::audio_dfx_test
