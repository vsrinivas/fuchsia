// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-client.h>

#include <ddk/debug.h>

namespace audio {

zx_status_t SimpleCodecClient::SetProtocol(ddk::CodecProtocolClient proto_client) {
  proto_client_ = proto_client;
  if (!proto_client_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }
  return ZX_OK;
}

void SimpleCodecClient::SetTimeout(int64_t nsecs) { timeout_nsecs_ = nsecs; }

zx_status_t SimpleCodecClient::Reset() {
  AsyncOut out = {};
  proto_client_.Reset(
      [](void* ctx, zx_status_t status) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->status = status;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return status;
  }
  if (out.status != ZX_OK) {
    return out.status;
  }
  return ZX_OK;
}

zx_status_t SimpleCodecClient::Stop() {
  AsyncOut out = {};
  proto_client_.Stop(
      [](void* ctx, zx_status_t status) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->status = status;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return status;
  }
  if (out.status != ZX_OK) {
    return out.status;
  }
  return ZX_OK;
}

zx_status_t SimpleCodecClient::Start() {
  AsyncOut out = {};
  proto_client_.Start(
      [](void* ctx, zx_status_t status) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->status = status;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return status;
  }
  if (out.status != ZX_OK) {
    return out.status;
  }
  return ZX_OK;
}

zx::status<Info> SimpleCodecClient::GetInfo() {
  AsyncOutData<Info> out = {};
  proto_client_.GetInfo(
      [](void* ctx, const info_t* info) {
        auto* out = reinterpret_cast<AsyncOutData<Info>*>(ctx);
        out->data.unique_id.assign(info->unique_id);
        out->data.product_name.assign(info->product_name);
        out->data.manufacturer.assign(info->manufacturer);
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(out.data);
}

zx::status<bool> SimpleCodecClient::IsBridgeable() {
  AsyncOutData<bool> out = {};
  proto_client_.IsBridgeable(
      [](void* ctx, bool supports_bridged_mode) {
        auto* out = reinterpret_cast<AsyncOutData<bool>*>(ctx);
        out->data = supports_bridged_mode;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(out.data);
}

zx_status_t SimpleCodecClient::SetBridgedMode(bool bridged) {
  AsyncOut out = {};
  proto_client_.SetBridgedMode(
      bridged,
      [](void* ctx) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        sync_completion_signal(&out->completion);
      },
      &out);
  return sync_completion_wait(&out.completion, timeout_nsecs_);
}

zx::status<std::vector<DaiSupportedFormats>> SimpleCodecClient::GetDaiFormats() {
  AsyncOutData<std::vector<DaiSupportedFormats>> out;
  proto_client_.GetDaiFormats(
      [](void* ctx, zx_status_t s, const dai_supported_formats_t* formats_list,
         size_t formats_count) {
        auto* out = reinterpret_cast<AsyncOutData<std::vector<DaiSupportedFormats>>*>(ctx);
        out->status = s;
        if (out->status == ZX_OK) {
          for (size_t i = 0; i < formats_count; ++i) {
            std::vector<uint32_t> number_of_channels(
                formats_list[i].number_of_channels_list,
                formats_list[i].number_of_channels_list + formats_list[i].number_of_channels_count);
            std::vector<sample_format_t> sample_formats(
                formats_list[i].sample_formats_list,
                formats_list[i].sample_formats_list + formats_list[i].sample_formats_count);
            std::vector<frame_format_t> frame_formats(
                formats_list[i].frame_formats_list,
                formats_list[i].frame_formats_list + formats_list[i].frame_formats_count);
            std::vector<uint32_t> frame_rates(
                formats_list[i].frame_rates_list,
                formats_list[i].frame_rates_list + formats_list[i].frame_rates_count);
            std::vector<uint8_t> bits_per_slot(
                formats_list[i].bits_per_slot_list,
                formats_list[i].bits_per_slot_list + formats_list[i].bits_per_slot_count);
            std::vector<uint8_t> bits_per_sample(
                formats_list[i].bits_per_sample_list,
                formats_list[i].bits_per_sample_list + formats_list[i].bits_per_sample_count);
            DaiSupportedFormats formats = {.number_of_channels = number_of_channels,
                                           .sample_formats = sample_formats,
                                           .frame_formats = frame_formats,
                                           .frame_rates = frame_rates,
                                           .bits_per_slot = bits_per_slot,
                                           .bits_per_sample = bits_per_sample};
            out->data.push_back(std::move(formats));
          }
        }
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  if (out.status != ZX_OK) {
    return zx::error(out.status);
  }
  return zx::ok(out.data);
}

zx_status_t SimpleCodecClient::SetDaiFormat(DaiFormat format) {
  AsyncOut out = {};
  dai_format_t f;
  f.number_of_channels = format.number_of_channels;
  f.channels_to_use_count = format.channels_to_use.size();
  auto channels_to_use = std::make_unique<uint32_t[]>(f.channels_to_use_count);
  for (size_t j = 0; j < f.channels_to_use_count; ++j) {
    channels_to_use[j] = format.channels_to_use[j];
  }
  f.channels_to_use_list = channels_to_use.get();
  f.sample_format = format.sample_format;
  f.frame_format = format.frame_format;
  f.frame_rate = format.frame_rate;
  f.bits_per_slot = format.bits_per_slot;
  f.bits_per_sample = format.bits_per_sample;
  proto_client_.SetDaiFormat(
      &f,
      [](void* ctx, zx_status_t s) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->status = s;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return status;
  }
  if (out.status != ZX_OK) {
    return out.status;
  }
  return status;
}

zx::status<GainFormat> SimpleCodecClient::GetGainFormat() {
  AsyncOutData<GainFormat> out = {};
  proto_client_.GetGainFormat(
      [](void* ctx, const gain_format_t* format) {
        auto* out = reinterpret_cast<AsyncOutData<GainFormat>*>(ctx);
        out->data.min_gain_db = format->min_gain;
        out->data.max_gain_db = format->max_gain;
        out->data.gain_step_db = format->gain_step;
        out->data.can_mute = format->can_mute;
        out->data.can_agc = format->can_agc;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(out.data);
}

zx::status<GainState> SimpleCodecClient::GetGainState() {
  AsyncOutData<GainState> out = {};
  proto_client_.GetGainState(
      [](void* ctx, const gain_state_t* state) {
        auto* out = reinterpret_cast<AsyncOutData<GainState>*>(ctx);
        out->data.gain_db = state->gain;
        out->data.muted = state->muted;
        out->data.agc_enable = state->agc_enable;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(out.data);
}

void SimpleCodecClient::SetGainState(GainState state) {
  gain_state_t state2 = {
      .gain = state.gain_db, .muted = state.muted, .agc_enable = state.agc_enable};
  proto_client_.SetGainState(
      &state2, [](void* ctx) {}, nullptr);
}

zx::status<PlugState> SimpleCodecClient::GetPlugState() {
  AsyncOutData<PlugState> out = {};
  proto_client_.GetPlugState(
      [](void* ctx, const plug_state_t* state) {
        auto* out = reinterpret_cast<AsyncOutData<PlugState>*>(ctx);
        out->data.hardwired = state->hardwired;
        out->data.plugged = state->plugged;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, timeout_nsecs_);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(out.data);
}

}  // namespace audio
