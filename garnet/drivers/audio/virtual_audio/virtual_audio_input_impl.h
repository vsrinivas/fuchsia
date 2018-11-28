// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_INPUT_IMPL_H_
#define GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_INPUT_IMPL_H_

#include <fuchsia/virtualaudio/cpp/fidl.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/unique_ptr.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>

#include "garnet/drivers/audio/virtual_audio/virtual_audio.h"
#include "garnet/drivers/audio/virtual_audio/virtual_audio_device_impl.h"
#include "garnet/drivers/audio/virtual_audio/virtual_audio_stream_in.h"

namespace virtual_audio {

class VirtualAudioControlImpl;

class VirtualAudioInputImpl : public VirtualAudioDeviceImpl,
                              public fuchsia::virtualaudio::Input {
 public:
  static fbl::unique_ptr<VirtualAudioInputImpl> Create(
      VirtualAudioControlImpl* owner) {
    return fbl::unique_ptr<VirtualAudioInputImpl>(
        new VirtualAudioInputImpl(owner));
  }

  bool CreateStream(zx_device_t* devnode) override {
    stream_ =
        ::audio::SimpleAudioStream::Create<VirtualAudioStreamIn>(this, devnode);
    return (stream_ != nullptr);
  }

  //
  // Config forwarding
  //
  void SetDeviceName(::std::string device_name) override {
    VirtualAudioConfigImpl::SetDeviceName(device_name);
  }

  void SetManufacturer(::std::string manufacturer_name) override {
    VirtualAudioConfigImpl::SetManufacturer(manufacturer_name);
  }

  void SetProduct(::std::string product_name) override {
    VirtualAudioConfigImpl::SetProduct(product_name);
  }

  void SetUniqueId(::fidl::Array<uint8_t, 16> unique_id) override {
    VirtualAudioConfigImpl::SetUniqueId(unique_id);
  }

  void AddFormatRange(uint32_t sample_format_flags, uint32_t min_frame_rate,
                      uint32_t max_frame_rate, uint8_t min_channels,
                      uint8_t max_channels,
                      uint16_t rate_family_flags) override {
    VirtualAudioConfigImpl::AddFormatRange(sample_format_flags, min_frame_rate,
                                           max_frame_rate, min_channels,
                                           max_channels, rate_family_flags);
  }

  void SetFifoDepth(uint32_t fifo_depth_bytes) override {
    VirtualAudioConfigImpl::SetFifoDepth(fifo_depth_bytes);
  }

  void SetExternalDelay(zx_duration_t external_delay) override {
    VirtualAudioConfigImpl::SetExternalDelay(external_delay);
  }

  void SetRingBufferRestrictions(uint32_t min_frames, uint32_t max_frames,
                                 uint32_t modulo_frames) override {
    VirtualAudioConfigImpl::SetRingBufferRestrictions(min_frames, max_frames,
                                                      modulo_frames);
  }

  void SetGainProperties(float min_gain_db, float max_gain_db,
                         float gain_step_db, float current_gain, bool can_mute,
                         bool current_mute, bool can_agc,
                         bool current_agc) override {
    VirtualAudioConfigImpl::SetGainProperties(
        min_gain_db, max_gain_db, gain_step_db, current_gain, can_mute,
        current_mute, can_agc, current_agc);
  }

  void SetPlugProperties(zx_time_t plug_change_time, bool plugged,
                         bool hardwired, bool can_notify) override {
    VirtualAudioConfigImpl::SetPlugProperties(plug_change_time, plugged,
                                              hardwired, can_notify);
  }

  void ResetConfig() override { Init(); }

  //
  // Device forwarding
  //
  void Add() override { VirtualAudioDeviceImpl::Add(); }

  void Remove() override { VirtualAudioDeviceImpl::Remove(); }

  void ChangePlugState(zx_time_t plug_change_time, bool plugged) override {
    VirtualAudioDeviceImpl::ChangePlugState(plug_change_time, plugged);
  }

  //
  // virtualaudio.Input interface
  //
  // ... empty, for now

 private:
  friend class fbl::unique_ptr<VirtualAudioInputImpl>;

  explicit VirtualAudioInputImpl(VirtualAudioControlImpl* owner)
      : VirtualAudioDeviceImpl(owner) {}
};

}  // namespace virtual_audio

#endif  // GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_INPUT_IMPL_H_
