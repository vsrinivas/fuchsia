// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <zircon/threads.h>

#include <fbl/auto_lock.h>

namespace audio {

SimpleCodecClient::~SimpleCodecClient() { Unbind(); }

SimpleCodecClient::SimpleCodecClient(SimpleCodecClient&& other) noexcept
    : loop_(&kAsyncLoopConfigNeverAttachToThread),
      created_with_dispatcher_(other.created_with_dispatcher_),
      dispatcher_(created_with_dispatcher_ ? other.dispatcher_ : loop_.dispatcher()) {
  other.Unbind();
  if (other.proto_client_.is_valid()) {
    SetProtocol(other.proto_client_);
  }
}

zx_status_t SimpleCodecClient::SetProtocol(ddk::CodecProtocolClient proto_client) {
  Unbind();

  proto_client_ = proto_client;
  if (!proto_client_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }
  zx::channel channel_remote;
  fidl::ClientEnd<fuchsia_hardware_audio::Codec> channel_local;
  auto status = zx::channel::create(0, &channel_local.channel(), &channel_remote);
  if (status != ZX_OK) {
    return status;
  }
  status = proto_client_.Connect(std::move(channel_remote));
  if (status != ZX_OK) {
    return status;
  }

  std::promise<void> codec_torn_down_promise;
  codec_torn_down_ = codec_torn_down_promise.get_future();
  codec_ = fidl::WireSharedClient(
      std::move(channel_local), dispatcher_,
      fidl::ObserveTeardown(
          [teardown = std::move(codec_torn_down_promise)]() mutable { teardown.set_value(); }));

  if (!created_with_dispatcher_ && !thread_started_) {
    status = loop_.StartThread("SimpleCodecClient thread");
    if (status != ZX_OK) {
      return status;
    }
    thread_started_ = true;
  }

  // The first call from this client shouldn't block.
  const auto response = codec_->WatchGainState_Sync();
  if (!response.ok()) {
    return response.status();
  }

  auto mutable_response = response.value();
  // Update the stored gain state, and start a hanging get to receive further gain state changes.
  UpdateGainState(&mutable_response);
  return ZX_OK;
}

zx_status_t SimpleCodecClient::Reset() { return codec_->Reset_Sync().status(); }

zx_status_t SimpleCodecClient::Stop() { return codec_->Stop_Sync().status(); }

zx_status_t SimpleCodecClient::Start() { return codec_->Start_Sync().status(); }

zx::status<Info> SimpleCodecClient::GetInfo() {
  const auto result = codec_->GetInfo_Sync();
  if (!result.ok()) {
    return zx::error(result.status());
  }

  const fuchsia_hardware_audio::wire::CodecInfo& llcpp_info = result.value().info;

  Info info;
  info.unique_id = std::string(llcpp_info.unique_id.data(), llcpp_info.unique_id.size());
  info.manufacturer = std::string(llcpp_info.manufacturer.data(), llcpp_info.manufacturer.size());
  info.product_name = std::string(llcpp_info.product_name.data(), llcpp_info.product_name.size());
  return zx::ok(std::move(info));
}

zx::status<bool> SimpleCodecClient::IsBridgeable() {
  const auto result = codec_->IsBridgeable_Sync();
  if (result.ok()) {
    return zx::ok(result.value().supports_bridged_mode);
  }
  return zx::error(result.status());
}

zx_status_t SimpleCodecClient::SetBridgedMode(bool bridged) {
  return codec_->SetBridgedMode(bridged).status();
}

zx::status<DaiSupportedFormats> SimpleCodecClient::GetDaiFormats() {
  auto result = codec_->GetDaiFormats_Sync();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  if (result.value().result.is_err()) {
    return zx::error(result.value().result.err());
  }

  ZX_ASSERT(result.value().result.response().formats.count() == 1);
  const auto& llcpp_formats = result.value().result.response().formats[0];

  DaiSupportedFormats formats;
  formats.number_of_channels = std::vector(llcpp_formats.number_of_channels.cbegin(),
                                           llcpp_formats.number_of_channels.cend());
  for (auto& sample_format : llcpp_formats.sample_formats) {
    formats.sample_formats.push_back(static_cast<SampleFormat>(sample_format));
  }
  for (auto& frame_format : llcpp_formats.frame_formats) {
    formats.frame_formats.push_back(static_cast<FrameFormat>(frame_format.frame_format_standard()));
  }
  formats.frame_rates =
      std::vector(llcpp_formats.frame_rates.cbegin(), llcpp_formats.frame_rates.cend());
  formats.bits_per_slot =
      std::vector(llcpp_formats.bits_per_slot.cbegin(), llcpp_formats.bits_per_slot.cend());
  formats.bits_per_sample =
      std::vector(llcpp_formats.bits_per_sample.cbegin(), llcpp_formats.bits_per_sample.cend());

  return zx::ok(formats);
}

zx::status<CodecFormatInfo> SimpleCodecClient::SetDaiFormat(DaiFormat format) {
  fidl::Arena allocator;

  fuchsia_hardware_audio::wire::DaiFormat format2;
  format2.number_of_channels = format.number_of_channels;
  format2.channels_to_use_bitmask = format.channels_to_use_bitmask;
  format2.sample_format =
      static_cast<fuchsia_hardware_audio::wire::DaiSampleFormat>(format.sample_format);
  const auto standard =
      static_cast<fuchsia_hardware_audio::wire::DaiFrameFormatStandard>(format.frame_format);
  format2.frame_format.set_frame_format_standard(allocator, standard);
  format2.frame_rate = format.frame_rate;
  format2.bits_per_slot = format.bits_per_slot;
  format2.bits_per_sample = format.bits_per_sample;

  const auto ret = codec_->SetDaiFormat_Sync(format2);
  if (!ret.ok()) {
    return zx::error(ret.status());
  }
  if (ret->result.is_err()) {
    return zx::error(ret->result.err());
  }
  CodecFormatInfo format_info = {};
  if (ret->result.response().state.has_external_delay()) {
    format_info.set_external_delay(ret->result.response().state.external_delay());
  }
  if (ret->result.response().state.has_turn_on_delay()) {
    format_info.set_turn_on_delay(ret->result.response().state.turn_on_delay());
  }
  return zx::ok(std::move(format_info));
}

zx::status<GainFormat> SimpleCodecClient::GetGainFormat() {
  const auto result = codec_->GetGainFormat_Sync();
  if (!result.ok()) {
    return zx::error(result.status());
  }

  const fuchsia_hardware_audio::wire::GainFormat& format = result.value().gain_format;

  // Only decibels in simple codec.
  ZX_ASSERT(format.type() == fuchsia_hardware_audio::wire::GainType::kDecibels);

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
  fbl::AutoLock lock(&gain_state_lock_);
  return gain_state_;
}

void SimpleCodecClient::SetGainState(GainState state) {
  fidl::Arena allocator;

  fuchsia_hardware_audio::wire::GainState state2(allocator);
  state2.set_gain_db(allocator, state.gain);
  state2.set_muted(allocator, state.muted);
  state2.set_agc_enabled(allocator, state.agc_enabled);
  const auto result = codec_->SetGainState(state2);
  if (result.ok()) {
    fbl::AutoLock lock(&gain_state_lock_);
    gain_state_ = zx::ok(state);
  }
}

void SimpleCodecClient::UpdateGainState(
    fidl::WireResponse<fuchsia_hardware_audio::Codec::WatchGainState>* response) {
  const GainState state{
      .gain = response->gain_state.gain_db(),
      .muted = response->gain_state.muted(),
      .agc_enabled = response->gain_state.agc_enabled(),
  };

  {
    fbl::AutoLock lock(&gain_state_lock_);
    gain_state_ = zx::ok(state);
  }

  codec_->WatchGainState(
      [&](fidl::WireResponse<fuchsia_hardware_audio::Codec::WatchGainState>* response2) {
        UpdateGainState(response2);
      });
}

void SimpleCodecClient::Unbind() {
  // Wait for any pending channel operations to complete. This ensures we don't get a WatchGainState
  // callback after the client has been freed.
  if (codec_.is_valid()) {
    codec_.AsyncTeardown();
    codec_torn_down_.wait();
  }
  {
    fbl::AutoLock lock(&gain_state_lock_);
    gain_state_ = zx::error(ZX_ERR_BAD_STATE);
  }
}

}  // namespace audio
