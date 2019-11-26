// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/examples/effects/delay_effect.h"

#include <cmath>
#include <optional>

#include <fbl/algorithm.h>
#include <rapidjson/document.h>

namespace media::audio_effects_example {
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
  if (delay_frames > DelayEffect::kMaxDelayFrames || delay_frames < DelayEffect::kMinDelayFrames) {
    return {};
  }

  return {{delay_frames}};
}

constexpr uint32_t ComputeDelaySamples(uint16_t channels_in, uint16_t delay_frames) {
  return channels_in * delay_frames;
}

}  // namespace

//
// DelayEffect: static member functions
//

// static -- called from static EffectBase::GetInfo; uses DelayEffect classwide consts
bool DelayEffect::GetInfo(fuchsia_audio_effects_description* desc) {
  strlcpy(desc->name, "Delay effect", sizeof(desc->name));
  desc->incoming_channels = kNumChannelsIn;
  desc->outgoing_channels = kNumChannelsOut;
  return true;
}

// static -- called from static EffectBase::Create
DelayEffect* DelayEffect::Create(uint32_t frame_rate, uint16_t channels_in, uint16_t channels_out,
                                 std::string_view config_json) {
  if (channels_in != channels_out) {
    return nullptr;
  }

  auto config = ParseConfig(config_json);
  if (!config) {
    return nullptr;
  }

  return new DelayEffect(frame_rate, channels_in, config->delay_frames);
}

//
// DelayEffect: instance member functions
//
DelayEffect::DelayEffect(uint32_t frame_rate, uint16_t channels, uint32_t delay_frames)
    : EffectBase(Effect::Delay, frame_rate, channels, channels, kLatencyFrames, kLatencyFrames) {
  // This buff must accommodate our maximum delay, plus the largest 'num_frames'
  // required by process_inplace -- which can be as large as frame_rate.
  delay_buff_ = std::make_unique<float[]>((kMaxDelayFrames + frame_rate) * channels);

  delay_samples_ = ComputeDelaySamples(channels_in_, delay_frames);
  ::memset(delay_buff_.get(), 0, delay_samples_ * sizeof(float));
}

bool DelayEffect::UpdateConfiguration(std::string_view config_json) {
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
bool DelayEffect::ProcessInplace(uint32_t num_frames, float* audio_buff) {
  if (delay_samples_ > 0) {
    uint32_t audio_buff_bytes = num_frames * channels_in_ * sizeof(float);

    // DelayEffect maintains a "delay cache" containing the next samples to emit.
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
bool DelayEffect::Flush() {
  ::memset(delay_buff_.get(), 0, delay_samples_ * sizeof(float));

  return true;
}

}  // namespace media::audio_effects_example
