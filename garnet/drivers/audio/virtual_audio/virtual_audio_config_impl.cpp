// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/audio/virtual_audio/virtual_audio_config_impl.h"

#include <fbl/algorithm.h>

namespace virtual_audio {

VirtualAudioConfigImpl::VirtualAudioConfigImpl() { Init(); }

VirtualAudioConfigImpl::~VirtualAudioConfigImpl() = default;

void VirtualAudioConfigImpl::Init() {
  snprintf(device_name_, sizeof(device_name_), kDefaultDeviceName);
  snprintf(mfr_name_, sizeof(mfr_name_), kDefaultManufacturerName);
  snprintf(prod_name_, sizeof(prod_name_), kDefaultProductName);
  memcpy(unique_id_, kDefaultUniqueId, sizeof(unique_id_));

  default_range_ = true;
  supported_formats_.clear();
  supported_formats_.push_back(kDefaultFormatRange);

  fifo_depth_ = kDefaultFifoDepthBytes;
  external_delay_nsec_ = kDefaultExternalDelayNsec;

  min_buffer_frames_ = kDefaultMinBufferFrames;
  max_buffer_frames_ = kDefaultMaxBufferFrames;
  modulo_buffer_frames_ = kDefaultModuloBufferFrames;

  cur_gain_state_ = kDefaultGainState;

  hardwired_ = kDefaultHardwired;
  async_plug_notify_ = kDefaultPlugCanNotify;
  plugged_ = kDefaultPlugged;

  plug_time_ = zx_clock_get(CLOCK_MONOTONIC);  // time of Config creation/reset
}

void VirtualAudioConfigImpl::SetDeviceName(const std::string& device_name) {
  strlcpy(device_name_, device_name.c_str(), sizeof(device_name_));
};

void VirtualAudioConfigImpl::SetManufacturer(
    const std::string& manufacturer_name) {
  strlcpy(mfr_name_, manufacturer_name.c_str(), sizeof(mfr_name_));
};

void VirtualAudioConfigImpl::SetProduct(const std::string& product_name) {
  strlcpy(prod_name_, product_name.c_str(), sizeof(prod_name_));
};

void VirtualAudioConfigImpl::SetUniqueId(
    const fidl::Array<uint8_t, 16>& unique_id) {
  memcpy(unique_id_, unique_id.data(), sizeof(unique_id_));
};

// After creation or reset, one default format range is always available.
// As soon as a format range is explicitly added, this default is removed.
void VirtualAudioConfigImpl::AddFormatRange(
    uint32_t format_flags, uint32_t min_rate, uint32_t max_rate,
    uint8_t min_chans, uint8_t max_chans, uint16_t rate_family_flags) {
  if (default_range_) {
    supported_formats_.clear();
    default_range_ = false;
  }

  audio_stream_format_range_t range = {.sample_formats = format_flags,
                                       .min_frames_per_second = min_rate,
                                       .max_frames_per_second = max_rate,
                                       .min_channels = min_chans,
                                       .max_channels = max_chans,
                                       .flags = rate_family_flags};

  supported_formats_.push_back(range);
};

void VirtualAudioConfigImpl::SetFifoDepth(uint32_t fifo_depth_bytes) {
  fifo_depth_ = fifo_depth_bytes;
}

void VirtualAudioConfigImpl::SetExternalDelay(zx_duration_t external_delay) {
  external_delay_nsec_ = external_delay;
};

void VirtualAudioConfigImpl::SetRingBufferRestrictions(uint32_t min_frames,
                                                       uint32_t max_frames,
                                                       uint32_t modulo_frames) {
  ZX_DEBUG_ASSERT(min_frames <= max_frames);
  ZX_DEBUG_ASSERT(min_frames % modulo_frames == 0);
  ZX_DEBUG_ASSERT(max_frames % modulo_frames == 0);

  min_buffer_frames_ = min_frames;
  max_buffer_frames_ = max_frames;
  modulo_buffer_frames_ = modulo_frames;
};

void VirtualAudioConfigImpl::SetGainProperties(float min_gain_db,
                                               float max_gain_db,
                                               float gain_step_db,
                                               float current_gain_db,
                                               bool can_mute, bool current_mute,
                                               bool can_agc, bool current_agc) {
  cur_gain_state_ = {.cur_mute = current_mute,
                     .cur_agc = current_agc,
                     .cur_gain = current_gain_db,

                     .can_mute = can_mute,
                     .can_agc = can_agc,

                     .min_gain = min_gain_db,
                     .max_gain = max_gain_db,
                     .gain_step = gain_step_db};
};

void VirtualAudioConfigImpl::SetPlugProperties(zx_time_t plug_change_time,
                                               bool plugged, bool hardwired,
                                               bool can_notify) {
  plug_time_ = plug_change_time;

  plugged_ = plugged;
  hardwired_ = hardwired;
  async_plug_notify_ = can_notify;
};

void VirtualAudioConfigImpl::ResetConfig() { Init(); };

}  // namespace virtual_audio
