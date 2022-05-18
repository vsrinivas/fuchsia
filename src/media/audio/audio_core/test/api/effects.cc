// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/media/audio/effects/audio_effects.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <string_view>

#include <rapidjson/document.h>

namespace {

enum class EffectId {
  // This effect inverts the sign of every sample.
  INVERTER = 0,
  // This effect sleeps for 20ms.
  SLEEPER = 1,
  // This effect doubles the incoming signal.
  DOUBLER = 2,
  // Total number of effects in this library.
  COUNT = 3
};

struct Effect {
  EffectId id;
  uint32_t frame_rate;
  uint16_t channels;
  bool enabled;
};

// This key enables basic testing of runtime changes to effect configurations:  { "enabled": bool }
// Also, a null or empty string results in an enabled effect (when no configuration is provided).
const char* kEnabled = "enabled";

zx_status_t ParseEnabledFromConfig(const char* config_cstr, size_t config_len, bool* enabled) {
  // By default an effect is enabled unless there is a scema error, or we confirm we are disabled.
  *enabled = true;

  // If no configuration, this (simple) effect is enabled.
  if (config_cstr == nullptr || !config_cstr[0]) {
    return ZX_OK;
  }

  rapidjson::Document doc;
  std::string parse_buffer(config_cstr);
  const rapidjson::ParseResult parse_res = doc.ParseInsitu(parse_buffer.data());
  // Invalid config string, so effect is disabled.
  if (parse_res.IsError()) {
    *enabled = false;
    return ZX_ERR_INVALID_ARGS;
  }

  const auto it = doc.FindMember(kEnabled);
  // Valid config, kEnabled key not found, so assume effect is enabled.
  if (it == doc.MemberEnd()) {
    return ZX_OK;
  }

  // kEnabled key was found, but its value isn't a bool. Invalid config, so effect is disabled.
  if (!it->value.IsBool()) {
    *enabled = false;
    return ZX_ERR_INVALID_ARGS;
  }
  // kEnabled key's value is a bool!  Are we enabled?
  *enabled = it->value.GetBool();

  return ZX_OK;
}

bool effect_get_info(uint32_t effect_id, fuchsia_audio_effects_description* desc) {
  if (desc == nullptr) {
    return false;
  }
  switch (effect_id) {
    case static_cast<uint32_t>(EffectId::INVERTER):
      strlcpy(desc->name, "inversion_filter", sizeof(desc->name));
      break;
    case static_cast<uint32_t>(EffectId::SLEEPER):
      strlcpy(desc->name, "sleeper_filter", sizeof(desc->name));
      break;
    case static_cast<uint32_t>(EffectId::DOUBLER):
      strlcpy(desc->name, "doubler_filter", sizeof(desc->name));
      break;
    default:
      return false;
  }
  desc->incoming_channels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY;
  desc->outgoing_channels = FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN;
  return true;
}

fuchsia_audio_effects_handle_t effect_create(uint32_t effect_id, uint32_t frame_rate,
                                             uint16_t channels_in, uint16_t channels_out,
                                             const char* config, size_t config_length) {
  if (channels_in != channels_out) {
    return FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  }
  switch (effect_id) {
    case static_cast<uint32_t>(EffectId::INVERTER):
    case static_cast<uint32_t>(EffectId::SLEEPER):
    case static_cast<uint32_t>(EffectId::DOUBLER):
      break;
    default:
      return FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  }
  bool enabled;
  zx_status_t status = ParseEnabledFromConfig(config, config_length, &enabled);
  if (status != ZX_OK) {
    return FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  }
  auto e = new Effect{
      .id = static_cast<EffectId>(effect_id),
      .frame_rate = frame_rate,
      .channels = channels_in,
      .enabled = enabled,
  };
  return reinterpret_cast<fuchsia_audio_effects_handle_t>(e);
}

bool effect_update_configuration(fuchsia_audio_effects_handle_t handle, const char* config,
                                 size_t config_length) {
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }
  bool enabled;
  zx_status_t status = ParseEnabledFromConfig(config, config_length, &enabled);
  if (status != ZX_OK) {
    return false;
  }
  reinterpret_cast<Effect*>(handle)->enabled = enabled;
  return true;
}

bool effect_delete(fuchsia_audio_effects_handle_t handle) {
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }
  delete reinterpret_cast<Effect*>(handle);
  return true;
}

bool effect_get_parameters(fuchsia_audio_effects_handle_t handle,
                           fuchsia_audio_effects_parameters* params) {
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || params == nullptr) {
    return false;
  }

  auto e = reinterpret_cast<Effect*>(handle);
  memset(params, 0, sizeof(*params));
  params->frame_rate = e->frame_rate;
  params->channels_in = e->channels;
  params->channels_out = e->channels;
  params->block_size_frames = FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY;
  params->signal_latency_frames = 0;
  params->max_frames_per_buffer = 0;
  return true;
}

bool effect_process_inplace(fuchsia_audio_effects_handle_t handle, uint32_t num_frames,
                            float* audio_buff_in_out) {
  if (handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || audio_buff_in_out == nullptr) {
    return false;
  }

  auto e = reinterpret_cast<Effect*>(handle);
  if (!e->enabled) {
    return true;
  }
  switch (e->id) {
    case EffectId::INVERTER:
      for (uint32_t k = 0; k < num_frames * e->channels; k++) {
        audio_buff_in_out[k] = -audio_buff_in_out[k];
      }
      return true;
    case EffectId::SLEEPER:
      usleep(20'000);
      return true;
    case EffectId::DOUBLER:
      for (uint32_t k = 0; k < num_frames * e->channels; k++) {
        audio_buff_in_out[k] *= 2.0f;
      }
      return true;
    default:
      return false;
  }
}

bool effect_process(fuchsia_audio_effects_handle_t handle, uint32_t num_frames,
                    const float* audio_buff_in, float** audio_buff_out) {
  return false;  // this library supports in-place effects only
}

bool effect_flush(fuchsia_audio_effects_handle_t handle) {
  return handle != FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
}

void effect_set_stream_info(fuchsia_audio_effects_handle_t handle,
                            const fuchsia_audio_effects_stream_info* stream_info) {}

}  // namespace

DECLARE_FUCHSIA_AUDIO_EFFECTS_MODULE_V1{
    static_cast<uint32_t>(EffectId::COUNT),  // num_effects
    &effect_get_info,
    &effect_create,
    &effect_update_configuration,
    &effect_delete,
    &effect_get_parameters,
    &effect_process_inplace,
    &effect_process,
    &effect_flush,
    &effect_set_stream_info,
};
