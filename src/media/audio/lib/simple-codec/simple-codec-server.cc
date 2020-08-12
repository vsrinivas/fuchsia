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

zx_status_t SimpleCodecServer::CreateInternal() {
  auto res = Initialize();
  if (res.is_error()) {
    return res.error_value();
  }
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

void SimpleCodecServer::CodecReset(codec_reset_callback callback, void* cookie) {
  callback(cookie, Reset());
}

void SimpleCodecServer::CodecStart(codec_start_callback callback, void* cookie) {
  callback(cookie, Start());
}

void SimpleCodecServer::CodecStop(codec_stop_callback callback, void* cookie) {
  callback(cookie, Stop());
}

void SimpleCodecServer::CodecGetInfo(codec_get_info_callback callback, void* cookie) {
  Info info = GetInfo();
  info_t info2 = {};
  info2.unique_id = info.unique_id.c_str();
  info2.manufacturer = info.manufacturer.c_str();
  info2.product_name = info.product_name.c_str();
  callback(cookie, &info2);
}

void SimpleCodecServer::CodecIsBridgeable(codec_is_bridgeable_callback callback, void* cookie) {
  callback(cookie, IsBridgeable());
}

void SimpleCodecServer::CodecSetBridgedMode(bool enable_bridged_mode,
                                            codec_set_bridged_mode_callback callback,
                                            void* cookie) {
  SetBridgedMode(enable_bridged_mode);
  callback(cookie);
}

void SimpleCodecServer::CodecGetDaiFormats(codec_get_dai_formats_callback callback, void* cookie) {
  std::vector<DaiSupportedFormats> formats = GetDaiFormats();
  auto formats2 = std::make_unique<dai_supported_formats_t[]>(formats.size());
  for (size_t i = 0; i < formats.size(); ++i) {
    formats2[i].number_of_channels_list = &formats[i].number_of_channels[0];
    formats2[i].number_of_channels_count = formats[i].number_of_channels.size();
    formats2[i].sample_formats_list = &formats[i].sample_formats[0];
    formats2[i].sample_formats_count = formats[i].sample_formats.size();
    formats2[i].justify_formats_list = &formats[i].justify_formats[0];
    formats2[i].justify_formats_count = formats[i].justify_formats.size();
    formats2[i].frame_rates_list = &formats[i].frame_rates[0];
    formats2[i].frame_rates_count = formats[i].frame_rates.size();
    formats2[i].bits_per_channel_list = &formats[i].bits_per_channel[0];
    formats2[i].bits_per_channel_count = formats[i].bits_per_channel.size();
    formats2[i].bits_per_sample_list = &formats[i].bits_per_sample[0];
    formats2[i].bits_per_sample_count = formats[i].bits_per_sample.size();
  }
  callback(cookie, ZX_OK, &formats2[0], formats.size());
}

void SimpleCodecServer::CodecSetDaiFormat(const dai_format_t* format,
                                          codec_set_dai_format_callback callback, void* cookie) {
  DaiFormat format2 = {};
  format2.number_of_channels = format->number_of_channels;
  for (size_t i = 0; i < format->channels_to_use_count; ++i) {
    format2.channels_to_use.push_back(format->channels_to_use_list[i]);
  }
  format2.sample_format = format->sample_format;
  format2.justify_format = format->justify_format;
  format2.frame_rate = format->frame_rate;
  format2.bits_per_channel = format->bits_per_channel;
  format2.bits_per_sample = format->bits_per_sample;
  callback(cookie, SetDaiFormat(std::move(format2)));
}

void SimpleCodecServer::CodecGetGainFormat(codec_get_gain_format_callback callback, void* cookie) {
  auto format = GetGainFormat();
  gain_format_t format2 = {};
  format2.type = GAIN_TYPE_DECIBELS;
  format2.min_gain = format.min_gain_db;
  format2.max_gain = format.max_gain_db;
  format2.gain_step = format.gain_step_db;
  format2.can_mute = format.can_mute;
  format2.can_agc = format.can_agc;
  callback(cookie, &format2);
}

void SimpleCodecServer::CodecSetGainState(const gain_state_t* gain_state,
                                          codec_set_gain_state_callback callback, void* cookie) {
  GainState gain_state2 = {};
  gain_state2.gain_db = gain_state->gain;
  gain_state2.muted = gain_state->muted;
  gain_state2.agc_enable = gain_state->agc_enable;
  SetGainState(gain_state2);
  callback(cookie);
}

void SimpleCodecServer::CodecGetGainState(codec_get_gain_state_callback callback, void* cookie) {
  GainState gain_state = GetGainState();
  gain_state_t gain_state2 = {};
  gain_state2.gain = gain_state.gain_db;
  gain_state2.muted = gain_state.muted;
  gain_state2.agc_enable = gain_state.agc_enable;
  callback(cookie, &gain_state2);
}

void SimpleCodecServer::CodecGetPlugState(codec_get_plug_state_callback callback, void* cookie) {
  PlugState plug_state = GetPlugState();
  plug_state_t plug_state2 = {};
  plug_state2.hardwired = plug_state.hardwired;
  plug_state2.plugged = plug_state.plugged;
  callback(cookie, &plug_state2);
}

}  // namespace audio
