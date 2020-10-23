// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-client.h>

#include <ddk/debug.h>

namespace audio {
namespace codec_fidl = ::fuchsia::hardware::audio::codec;
using SyncCall = ::fuchsia::hardware::audio::codec::Codec_Sync;

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
void SimpleCodecClient::SetTimeout(int64_t nsecs) { timeout_nsecs_ = nsecs; }

zx_status_t SimpleCodecClient::Reset() {
  int32_t out_status = 0;
  auto status = codec_->Reset(&out_status);
  return (status == ZX_OK) ? out_status : status;
}

zx_status_t SimpleCodecClient::Stop() {
  int32_t out_status = 0;
  auto status = codec_->Stop(&out_status);
  return (status == ZX_OK) ? out_status : status;
}

zx_status_t SimpleCodecClient::Start() {
  int32_t out_status = 0;
  auto status = codec_->Start(&out_status);
  return (status == ZX_OK) ? out_status : status;
}

zx::status<Info> SimpleCodecClient::GetInfo() {
  codec_fidl::Info info = {};
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

zx::status<std::vector<DaiSupportedFormats>> SimpleCodecClient::GetDaiFormats() {
  int32_t out_status = 0;
  std::vector<DaiSupportedFormats> out_formats;
  auto status = codec_->GetDaiFormats(&out_status, &out_formats);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(out_formats));
}

zx_status_t SimpleCodecClient::SetDaiFormat(DaiFormat format) {
  int32_t out_status = 0;
  auto status = codec_->SetDaiFormat(std::move(format), &out_status);
  return (status == ZX_OK) ? out_status : status;
}

zx::status<GainFormat> SimpleCodecClient::GetGainFormat() {
  codec_fidl::GainFormat gain_format = {};
  auto status = codec_->GetGainFormat(&gain_format);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(gain_format));
}

zx::status<GainState> SimpleCodecClient::GetGainState() {
  codec_fidl::GainState gain_state = {};
  auto status = codec_->GetGainState(&gain_state);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(gain_state));
}

void SimpleCodecClient::SetGainState(GainState state) {
  auto unused = codec_->SetGainState(std::move(state));
  static_cast<void>(unused);
}

zx::status<PlugState> SimpleCodecClient::GetPlugState() {
  codec_fidl::PlugState plug_state = {};
  auto status = codec_->GetPlugState(&plug_state);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(plug_state));
}

}  // namespace audio
