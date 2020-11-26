// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-client.h>

#include <ddk/debug.h>

namespace audio {

namespace audio_fidl = ::fuchsia::hardware::audio;
using SyncCall = ::fuchsia::hardware::audio::Codec_Sync;

zx_status_t SimpleCodecClient::SetProtocol(ddk::CodecProtocolClient proto_client) {
  proto_client_ = proto_client;
  if (!proto_client_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }
  zx::channel channel_remote, channel_local;
  auto status = zx::channel::create(0, &channel_local, &channel_remote);
  if (status != ZX_OK) {
    return status;
  }
  status = proto_client_.Connect(std::move(channel_remote));
  if (status != ZX_OK) {
    return status;
  }
  codec_.Bind(std::move(channel_local));
  return ZX_OK;
}

zx_status_t SimpleCodecClient::Reset() { return codec_->Reset(); }

zx_status_t SimpleCodecClient::Stop() { return codec_->Stop(); }

zx_status_t SimpleCodecClient::Start() { return codec_->Start(); }

zx::status<Info> SimpleCodecClient::GetInfo() {
  Info info = {};
  auto status = codec_->GetInfo(&info);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(info));
}

zx::status<bool> SimpleCodecClient::IsBridgeable() {
  bool out_supports_bridged_mode = false;
  auto status = codec_->IsBridgeable(&out_supports_bridged_mode);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(out_supports_bridged_mode);
}

zx_status_t SimpleCodecClient::SetBridgedMode(bool bridged) {
  return codec_->SetBridgedMode(bridged);
}

zx::status<DaiSupportedFormats> SimpleCodecClient::GetDaiFormats() {
  audio_fidl::Codec_GetDaiFormats_Result result;
  auto status = codec_->GetDaiFormats(&result);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  ZX_ASSERT(result.response().formats.size() == 1);
  std::vector<FrameFormat> frame_formats;
  auto& formats = result.response().formats[0];
  for (auto& frame_format : formats.frame_formats) {
    frame_formats.push_back(frame_format.frame_format_standard());
  }
  return zx::ok(DaiSupportedFormats{
      .number_of_channels = std::move(formats.number_of_channels),
      .sample_formats = std::move(formats.sample_formats),
      .frame_formats = std::move(frame_formats),
      .frame_rates = std::move(formats.frame_rates),
      .bits_per_slot = std::move(formats.bits_per_slot),
      .bits_per_sample = std::move(formats.bits_per_sample),
  });
}

zx_status_t SimpleCodecClient::SetDaiFormat(DaiFormat format) {
  int32_t out_status = ZX_OK;
  audio_fidl::DaiFormat format2;
  format2.number_of_channels = format.number_of_channels;
  format2.channels_to_use_bitmask = format.channels_to_use_bitmask;
  format2.sample_format = format.sample_format;
  format2.frame_format.set_frame_format_standard(format.frame_format);
  format2.frame_rate = format.frame_rate;
  format2.bits_per_slot = format.bits_per_slot;
  format2.bits_per_sample = format.bits_per_sample;
  auto status = codec_->SetDaiFormat(std::move(format2), &out_status);
  return (status == ZX_OK) ? out_status : status;
}

zx::status<GainFormat> SimpleCodecClient::GetGainFormat() {
  audio_fidl::GainFormat format = {};
  auto status = codec_->GetGainFormat(&format);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  ZX_ASSERT(format.type() == audio_fidl::GainType::DECIBELS);  // Only decibels in simple codec.
  // Only hardwired in simple codec.
  return zx::ok(GainFormat{
      .min_gain = format.min_gain(),
      .max_gain = format.max_gain(),
      .gain_step = format.gain_step(),
      .can_mute = format.can_mute(),
      .can_agc = format.can_agc(),
  });
}

zx::status<GainState> SimpleCodecClient::GetGainState() {
  // Only watch the first time.
  static bool first_time = true;
  if (first_time) {
    ::fuchsia::hardware::audio::GainState out_gain_state;
    auto status = codec_->WatchGainState(&out_gain_state);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    gain_state_.gain = out_gain_state.gain_db();
    gain_state_.muted = out_gain_state.muted();
    gain_state_.agc_enabled = out_gain_state.agc_enabled();
    first_time = false;
  }
  return zx::ok(gain_state_);
}

void SimpleCodecClient::SetGainState(GainState state) {
  audio_fidl::GainState state2;
  state2.set_gain_db(state.gain);
  state2.set_muted(state.muted);
  state2.set_agc_enabled(state.agc_enabled);
  gain_state_ = state;
  auto unused = codec_->SetGainState(std::move(state2));
  static_cast<void>(unused);
}

}  // namespace audio
