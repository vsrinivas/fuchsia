// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/effects/test_effects/test_effects_v1.h"

#include <string>
#include <string_view>

extern fuchsia_audio_effects_module_v1 fuchsia_audio_effects_module_v1_instance;

namespace {

static constexpr uint32_t TEST_EFFECTS_MAX = 255;
static constexpr uint32_t TEST_EFFECTS_DEFAULT_MAX_FRAMES_PER_BATCH = 512;

test_effect_v1_spec g_effects[TEST_EFFECTS_MAX] = {};

uint32_t g_instance_count = 0;

class TestEffect {
 public:
  TestEffect(uint32_t effect_id, uint32_t frame_rate, uint16_t chans_in, uint16_t chans_out,
             std::string_view config)
      : effect_id_(effect_id),
        frame_rate_(frame_rate),
        channels_in_(chans_in),
        channels_out_(chans_out),
        config_(config) {
    // If this is an out-of-place effect, allocate our output buffer.
    if (channels_in() != channels_out()) {
      out_of_place_buffer_frames_ = max_batch_size();
      if (out_of_place_buffer_frames_ == FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY) {
        out_of_place_buffer_frames_ = TEST_EFFECTS_DEFAULT_MAX_FRAMES_PER_BATCH;
      }
      out_of_place_buffer_ =
          std::make_unique<float[]>(out_of_place_buffer_frames_ * channels_out());
    }
  }

  uint32_t effect_id() const { return effect_id_; }
  uint32_t frame_rate() const { return frame_rate_; }
  uint16_t channels_in() const { return channels_in_; }
  uint16_t channels_out() const {
    return channels_out_ == FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN ? channels_in_
                                                                      : channels_out_;
  }
  uint32_t block_size_frames() const { return g_effects[effect_id()].block_size_frames; }
  uint32_t max_batch_size() const { return g_effects[effect_id()].max_batch_size; }
  uint32_t latency_frames() const { return g_effects[effect_id()].signal_latency_frames; }
  uint32_t ring_out_frames() const { return g_effects[effect_id()].ring_out_frames; }
  std::string_view config() const { return config_; }
  size_t flush_count() const { return flush_count_; }

  bool UpdateConfiguration(std::string_view new_config) {
    config_ = new_config;
    return true;
  }

  bool GetParameters(fuchsia_audio_effects_parameters* params) const {
    memset(params, 0, sizeof(*params));
    params->frame_rate = frame_rate();
    params->channels_in = channels_in();
    params->channels_out = channels_out();
    params->block_size_frames = block_size_frames();
    params->max_frames_per_buffer = max_batch_size();
    params->signal_latency_frames = latency_frames();
    params->ring_out_frames = ring_out_frames();
    return true;
  }

  bool ProcessInPlace(uint32_t num_frames, float* audio_buff_in_out) {
    if (channels_in() != channels_out()) {
      return false;
    }
    auto& effect = g_effects[effect_id()];
    for (uint32_t i = 0; i < num_frames * channels_in(); ++i) {
      if (effect.action == TEST_EFFECTS_ACTION_ADD) {
        audio_buff_in_out[i] = audio_buff_in_out[i] + effect.value;
      } else if (effect.action == TEST_EFFECTS_ACTION_ASSIGN) {
        audio_buff_in_out[i] = effect.value;
      } else if (effect.action == TEST_EFFECTS_ACTION_ASSIGN_CONFIG_SIZE) {
        audio_buff_in_out[i] = static_cast<float>(config_.size());
      } else {
        return false;
      }
    }
    return true;
  }

  bool Process(uint32_t num_frames, const float* audio_buff_in, float** audio_buff_out) {
    // If channels are equal, ProcessInPlace should be used.
    if (channels_in() == channels_out()) {
      return false;
    }
    if (!out_of_place_buffer_) {
      return false;
    }
    if (num_frames > out_of_place_buffer_frames_) {
      return false;
    }

    *audio_buff_out = out_of_place_buffer_.get();
    auto& effect = g_effects[effect_id()];
    for (uint32_t i = 0; i < num_frames; ++i) {
      for (int32_t j = 0; j < channels_out(); ++j) {
        if (effect.action == TEST_EFFECTS_ACTION_ADD) {
          if (j < channels_in()) {
            (*audio_buff_out)[i * channels_out() + j] =
                audio_buff_in[i * channels_in() + j] + effect.value;
          } else {
            (*audio_buff_out)[i * channels_out() + j] = 0.0;
          }
        } else if (effect.action == TEST_EFFECTS_ACTION_ASSIGN) {
          (*audio_buff_out)[i * channels_out() + j] = effect.value;
        } else {
          return false;
        }
      }
    }
    return true;
  }

  bool Flush() {
    flush_count_++;
    return true;
  }

  void SetStreamInfo(const fuchsia_audio_effects_stream_info* stream_info) {
    stream_info_ = *stream_info;
  }

  zx_status_t Inspect(test_effects_v1_inspect_state* state) {
    state->config = config_.data();
    state->config_length = config_.size();
    state->effect_id = effect_id();
    state->flush_count = flush_count();
    state->stream_info = stream_info_;
    return ZX_OK;
  }

 private:
  uint32_t effect_id_;
  uint32_t frame_rate_;
  uint16_t channels_in_;
  uint16_t channels_out_;
  std::string config_;

  fuchsia_audio_effects_stream_info stream_info_ = {};

  size_t flush_count_ = 0;

  std::unique_ptr<float[]> out_of_place_buffer_;
  size_t out_of_place_buffer_frames_;
};

bool get_info(uint32_t effect_id, fuchsia_audio_effects_description* desc) {
  if (effect_id >= fuchsia_audio_effects_module_v1_instance.num_effects) {
    return false;
  }
  *desc = g_effects[effect_id].description;
  return true;
}

fuchsia_audio_effects_handle_t create_effect(uint32_t effect_id, uint32_t frame_rate,
                                             uint16_t channels_in, uint16_t channels_out,
                                             const char* config, size_t config_length) {
  if (effect_id > TEST_EFFECTS_MAX || channels_in > FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX ||
      channels_out > FUCHSIA_AUDIO_EFFECTS_CHANNELS_MAX) {
    return FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE;
  }

  g_instance_count++;
  return reinterpret_cast<fuchsia_audio_effects_handle_t>(
      new TestEffect(effect_id, frame_rate, channels_in, channels_out, {config, config_length}));
}

bool delete_effect(fuchsia_audio_effects_handle_t effects_handle) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }
  --g_instance_count;
  delete reinterpret_cast<TestEffect*>(effects_handle);
  return true;
}

bool update_effect_configuration(fuchsia_audio_effects_handle_t effects_handle, const char* config,
                                 size_t config_length) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }
  return reinterpret_cast<TestEffect*>(effects_handle)
      ->UpdateConfiguration({config, config_length});
}

bool get_parameters(fuchsia_audio_effects_handle_t effects_handle,
                    fuchsia_audio_effects_parameters* params) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || params == nullptr) {
    return false;
  }
  return reinterpret_cast<TestEffect*>(effects_handle)->GetParameters(params);
}

bool process_inplace(fuchsia_audio_effects_handle_t effects_handle, uint32_t num_frames,
                     float* audio_buff_in_out) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || audio_buff_in_out == nullptr) {
    return false;
  }
  return reinterpret_cast<TestEffect*>(effects_handle)
      ->ProcessInPlace(num_frames, audio_buff_in_out);
}

bool process(fuchsia_audio_effects_handle_t effects_handle, uint32_t num_frames,
             const float* audio_buff_in, float** audio_buff_out) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE || !audio_buff_in || !audio_buff_out) {
    return false;
  }
  return reinterpret_cast<TestEffect*>(effects_handle)
      ->Process(num_frames, audio_buff_in, audio_buff_out);
}

bool flush(fuchsia_audio_effects_handle_t effects_handle) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return false;
  }
  return reinterpret_cast<TestEffect*>(effects_handle)->Flush();
}

void set_stream_info(fuchsia_audio_effects_handle_t effects_handle,
                     const fuchsia_audio_effects_stream_info* stream_info) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return;
  }
  return reinterpret_cast<TestEffect*>(effects_handle)->SetStreamInfo(stream_info);
}

zx_status_t ext_add_effect(test_effect_v1_spec effect) {
  if (g_instance_count > 0) {
    return ZX_ERR_BAD_STATE;
  }
  auto id = fuchsia_audio_effects_module_v1_instance.num_effects;
  if (id > TEST_EFFECTS_MAX) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  g_effects[id] = effect;
  fuchsia_audio_effects_module_v1_instance.num_effects++;
  return ZX_OK;
}

zx_status_t ext_clear_effects() {
  if (g_instance_count > 0) {
    return ZX_ERR_BAD_STATE;
  }
  fuchsia_audio_effects_module_v1_instance.num_effects = 0;
  return ZX_OK;
}

zx_status_t ext_inspect_instance(fuchsia_audio_effects_handle_t effects_handle,
                                 test_effects_v1_inspect_state* state) {
  if (effects_handle == FUCHSIA_AUDIO_EFFECTS_INVALID_HANDLE) {
    return ZX_ERR_INVALID_ARGS;
  }
  return reinterpret_cast<TestEffect*>(effects_handle)->Inspect(state);
}

uint32_t ext_num_instances() { return g_instance_count; }

}  // namespace

DECLARE_FUCHSIA_AUDIO_EFFECTS_MODULE_V1{
    0,
    &get_info,
    &create_effect,
    &update_effect_configuration,
    &delete_effect,
    &get_parameters,
    &process_inplace,
    &process,
    &flush,
    &set_stream_info,
};

DECLARE_TEST_EFFECTS_V1_EXT{
    &ext_add_effect,
    &ext_clear_effects,
    &ext_num_instances,
    &ext_inspect_instance,
};
