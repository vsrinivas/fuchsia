// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_
#define GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_

#include <fuchsia/virtualaudio/cpp/fidl.h>

#include <audio-proto/audio-proto.h>
#include <fbl/ref_ptr.h>

#include "garnet/drivers/audio/virtual_audio/virtual_audio_control_impl.h"

namespace virtual_audio {

class VirtualAudioStream;

class VirtualAudioDeviceImpl : public fuchsia::virtualaudio::Input,
                               public fuchsia::virtualaudio::Output {
 public:
  static constexpr char kDefaultDeviceName[] = "Virtual_Audio_Device_(default)";
  static constexpr char kDefaultManufacturerName[] =
      "Fuchsia Virtual Audio Group (default manufacturer name********)";
  static constexpr char kDefaultProductName[] =
      "Virgil v1 (default unchanged product name*********************)";
  static constexpr uint8_t kDefaultUniqueId[16] = {
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0};

  // One very limited range for basic audio support by default.
  static constexpr audio_stream_format_range_t kDefaultFormatRange = {
      .min_channels = 2,
      .max_channels = 2,
      .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
      .min_frames_per_second = 48000,
      .max_frames_per_second = 48000,
      .flags = ASF_RANGE_FLAG_FPS_48000_FAMILY};

  // Default FIFO is 1 msec, at 48k stereo 16
  static constexpr uint32_t kDefaultFifoDepthBytes = 192;
  static constexpr zx_time_t kDefaultExternalDelayNsec = 0;

  // At default rate 48k, this is 50 msec
  static constexpr uint32_t kDefaultMinBufferFrames = 2400;
  // At default rate 48k, this is 10+ sec!
  static constexpr uint32_t kDefaultMaxBufferFrames = 1 << 19;
  static constexpr uint32_t kDefaultModuloBufferFrames = 4;

  static constexpr ::audio::audio_proto::GetGainResp kDefaultGainState = {
      .cur_mute = false,
      .cur_agc = false,
      .cur_gain = 0.0f,
      .can_mute = true,
      .can_agc = false,
      .min_gain = -160.0f,
      .max_gain = 24.0f,
      .gain_step = 0.25f};

  static constexpr bool kDefaultPlugged = true;
  static constexpr bool kDefaultHardwired = false;
  static constexpr bool kDefaultPlugCanNotify = true;

  static fbl::unique_ptr<VirtualAudioDeviceImpl> Create(
      VirtualAudioControlImpl* owner, bool is_input);

  // Execute the given task on the FIDL channel's main dispatcher thread.
  // Used to deliver callbacks or events, from the driver execution domain.
  void PostToDispatcher(fit::closure task_to_post);

  virtual bool CreateStream(zx_device_t* devnode);
  void RemoveStream();
  void ClearStream();

  void Init();

  //
  // virtualaudio.Configuration interface
  //
  void SetDeviceName(std::string device_name) override;
  void SetManufacturer(std::string manufacturer_name) override;
  void SetProduct(std::string product_name) override;
  void SetUniqueId(fidl::Array<uint8_t, 16> unique_id) override;

  void AddFormatRange(uint32_t format_flags, uint32_t min_rate,
                      uint32_t max_rate, uint8_t min_chans, uint8_t max_chans,
                      uint16_t rate_family_flags) override;

  void SetFifoDepth(uint32_t fifo_depth_bytes) override;
  void SetExternalDelay(zx_duration_t external_delay) override;
  void SetRingBufferRestrictions(uint32_t min_frames, uint32_t max_frames,
                                 uint32_t modulo_frames) override;
  void SetGainProperties(float min_gain_db, float max_gain_db,
                         float gain_step_db, float current_gain_db,
                         bool can_mute, bool current_mute, bool can_agc,
                         bool current_agc) override;

  void SetPlugProperties(zx_time_t plug_change_time, bool plugged,
                         bool hardwired, bool can_notify) override;

  void ResetConfiguration() override;

  //
  // virtualaudio.Device interface
  //
  void Add() override;
  void Remove() override;
  void ChangePlugState(zx_time_t plug_change_time, bool plugged) override;

 protected:
  friend class VirtualAudioStream;
  friend class fbl::unique_ptr<VirtualAudioDeviceImpl>;

  VirtualAudioDeviceImpl(VirtualAudioControlImpl* owner, bool is_input);
  virtual ~VirtualAudioDeviceImpl();

  VirtualAudioControlImpl const* owner_;
  fbl::RefPtr<VirtualAudioStream> stream_;
  bool is_input_;

  char device_name_[32];
  char mfr_name_[64];
  char prod_name_[64];
  uint8_t unique_id_[16];

  std::vector<audio_stream_format_range_t> supported_formats_;

  uint32_t fifo_depth_;
  zx_duration_t external_delay_nsec_;

  uint32_t min_buffer_frames_;
  uint32_t max_buffer_frames_;
  uint32_t modulo_buffer_frames_;

  ::audio::audio_proto::GetGainResp cur_gain_state_;

  zx_time_t plug_time_;
  bool plugged_;
  bool hardwired_;
  bool async_plug_notify_;

  bool default_range_ = true;
};

}  // namespace virtual_audio

#endif  // GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_
