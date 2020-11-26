// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-server.h>

#include <algorithm>
#include <memory>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace audio {

namespace audio_fidl = ::fuchsia::hardware::audio;

zx_status_t SimpleCodecServer::CreateInternal() {
  auto res = Initialize();
  if (res.is_error()) {
    return res.error_value();
  }
  loop_.StartThread();
  driver_ids_ = res.value();
  Info info = GetInfo();
  if (driver_ids_.instance_count != 0) {
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, driver_ids_.vendor_id},
        {BIND_PLATFORM_DEV_DID, 0, driver_ids_.device_id},
        {BIND_CODEC_INSTANCE, 0, driver_ids_.instance_count},
    };
    return DdkAdd(ddk::DeviceAddArgs(info.product_name.c_str()).set_props(props));
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, driver_ids_.vendor_id},
      {BIND_PLATFORM_DEV_DID, 0, driver_ids_.device_id},
  };
  return DdkAdd(ddk::DeviceAddArgs(info.product_name.c_str()).set_props(props));
}

zx_status_t SimpleCodecServer::CodecConnect(zx::channel channel) {
  binding_.emplace(this, std::move(channel), loop_.dispatcher());
  return ZX_OK;
}

template <class T>
SimpleCodecServerInternal<T>::SimpleCodecServerInternal() {
  plug_time_ = zx::clock::get_monotonic().get();
}

template <class T>
void SimpleCodecServerInternal<T>::Reset(ResetCallback callback) {
  auto status = static_cast<T*>(this)->Reset();
  if (status != ZX_OK) {
    static_cast<T*>(this)->binding_->Unbind();
  }
  callback();
}

template <class T>
void SimpleCodecServerInternal<T>::Stop(StopCallback callback) {
  auto status = static_cast<T*>(this)->Stop();
  if (status != ZX_OK) {
    static_cast<T*>(this)->binding_->Unbind();
  }
  callback();
}

template <class T>
void SimpleCodecServerInternal<T>::Start(StartCallback callback) {
  auto status = static_cast<T*>(this)->Start();
  if (status != ZX_OK) {
    static_cast<T*>(this)->binding_->Unbind();
  }
  callback();
}

template <class T>
void SimpleCodecServerInternal<T>::GetInfo(GetInfoCallback callback) {
  callback(static_cast<T*>(this)->GetInfo());
}

template <class T>
void SimpleCodecServerInternal<T>::IsBridgeable(IsBridgeableCallback callback) {
  callback(static_cast<T*>(this)->IsBridgeable());
}

template <class T>
void SimpleCodecServerInternal<T>::GetDaiFormats(GetDaiFormatsCallback callback) {
  auto formats = static_cast<T*>(this)->GetDaiFormats();
  std::vector<audio_fidl::DaiFrameFormat> frame_formats;
  for (FrameFormat i : formats.frame_formats) {
    audio_fidl::DaiFrameFormat frame_format;
    frame_format.set_frame_format_standard(i);
    frame_formats.emplace_back(std::move(frame_format));
  }
  audio_fidl::Codec_GetDaiFormats_Response response;
  response.formats.emplace_back(audio_fidl::DaiSupportedFormats{
      .number_of_channels = std::move(formats.number_of_channels),
      .sample_formats = std::move(formats.sample_formats),
      .frame_formats = std::move(frame_formats),
      .frame_rates = std::move(formats.frame_rates),
      .bits_per_slot = std::move(formats.bits_per_slot),
      .bits_per_sample = std::move(formats.bits_per_sample),
  });
  audio_fidl::Codec_GetDaiFormats_Result result;
  result.set_response(std::move(response));
  callback(std::move(result));
}

template <class T>
void SimpleCodecServerInternal<T>::SetDaiFormat(audio_fidl::DaiFormat format,
                                                SetDaiFormatCallback callback) {
  DaiFormat format2 = {};
  format2.number_of_channels = format.number_of_channels;
  format2.channels_to_use_bitmask = format.channels_to_use_bitmask;
  format2.sample_format = format.sample_format;
  format2.frame_format = format.frame_format.frame_format_standard();
  format2.frame_rate = format.frame_rate;
  format2.bits_per_slot = format.bits_per_slot;
  format2.bits_per_sample = format.bits_per_sample;
  callback(static_cast<T*>(this)->SetDaiFormat(std::move(format2)));
}

template <class T>
void SimpleCodecServerInternal<T>::GetGainFormat(GetGainFormatCallback callback) {
  auto format = static_cast<T*>(this)->GetGainFormat();
  audio_fidl::GainFormat format2;
  format2.set_type(audio_fidl::GainType::DECIBELS);  // Only decibels in simple codec.
  format2.set_min_gain(format.min_gain);
  format2.set_max_gain(format.max_gain);
  format2.set_gain_step(format.gain_step);
  format2.set_can_mute(format.can_mute);
  format2.set_can_agc(format.can_agc);
  callback(std::move(format2));
}

template <class T>
void SimpleCodecServerInternal<T>::WatchGainState(WatchGainStateCallback callback) {
  audio_fidl::GainState gain_state;
  // Only reply the first time, then don't reply anymore. In simple codecs gain must only be
  // changed by SetGainState and hence we don't expect any watch calls to determine gain changes.
  static bool first_time = true;
  if (first_time) {
    auto state = static_cast<T*>(this)->GetGainState();
    gain_state.set_muted(state.muted);
    gain_state.set_agc_enabled(state.agc_enabled);
    gain_state.set_gain_db(state.gain);
    callback(std::move(gain_state));
    first_time = false;
  }
}

template <class T>
void SimpleCodecServerInternal<T>::SetGainState(audio_fidl::GainState state) {
  GainState state2;
  state2.gain = state.gain_db();
  state2.muted = state.muted();
  state2.agc_enabled = state.agc_enabled();
  static_cast<T*>(this)->SetGainState(std::move(state2));
}

template <class T>
void SimpleCodecServerInternal<T>::GetPlugDetectCapabilities(
    GetPlugDetectCapabilitiesCallback callback) {
  // Only hardwired in simple codec.
  callback(::fuchsia::hardware::audio::PlugDetectCapabilities::HARDWIRED);
}

template <class T>
void SimpleCodecServerInternal<T>::WatchPlugState(WatchPlugStateCallback callback) {
  audio_fidl::PlugState plug_state;
  // Only reply the first time, then don't reply anymore. Simple codec does not support changes to
  // plug state, also clients using simple codec do not issue WatchPlugState calls.
  static bool first_time = true;
  if (first_time) {
    plug_state.set_plugged(true);
    plug_state.set_plug_state_time(plug_time_);
    callback(std::move(plug_state));
    first_time = false;
  }
}

template class SimpleCodecServerInternal<SimpleCodecServer>;

}  // namespace audio
