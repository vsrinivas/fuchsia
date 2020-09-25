// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_
#define SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_

#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/closure-queue/closure_queue.h>
#include <lib/zx/clock.h>

#include <memory>

#include <audio-proto/audio-proto.h>
#include <fbl/ref_ptr.h>

#include "src/media/audio/drivers/virtual_audio/virtual_audio_control_impl.h"

namespace virtual_audio {

class VirtualAudioStream;

class VirtualAudioDeviceImpl : public fuchsia::virtualaudio::Input,
                               public fuchsia::virtualaudio::Output {
 public:
  static constexpr char kDefaultDeviceName[] = "Virtual_Audio_Device_(default)";
  static constexpr char kDefaultManufacturerName[] =
      "Fuchsia Virtual Audio Group (*** default manufacturer name "
      "********************************************************************************************"
      "********************************************************************************************"
      "***********)";
  static constexpr char kDefaultProductName[] =
      "Virgil v1, a Virtual Volume Vessel (** default product name "
      "********************************************************************************************"
      "********************************************************************************************"
      "**********)";

  static constexpr uint8_t kDefaultUniqueId[16] = {1, 2,  3,  4,  5,  6,  7,  8,
                                                   9, 10, 11, 12, 13, 14, 15, 0};

  // One very limited range for basic audio support by default.
  static constexpr audio_stream_format_range_t kDefaultFormatRange = {
      .sample_formats = AUDIO_SAMPLE_FORMAT_16BIT,
      .min_frames_per_second = 48000,
      .max_frames_per_second = 48000,
      .min_channels = 2,
      .max_channels = 2,
      .flags = ASF_RANGE_FLAG_FPS_48000_FAMILY,
  };

  // Default clock domain is 0, the local system CLOCK_MONOTONIC domain
  static constexpr int32_t kDefaultClockDomain = 0;
  static constexpr int32_t kDefaultClockRateAdjustment = 0;

  // Default FIFO is 250 usec, at 48k stereo 16
  static constexpr uint32_t kDefaultFifoDepthBytes = 48;
  static constexpr zx_time_t kDefaultExternalDelayNsec = 0;

  // Default ring buffer size is at least 250msec (assuming default rate 48k)
  static constexpr uint32_t kDefaultMinBufferFrames = 12000;
  static constexpr uint32_t kDefaultMaxBufferFrames = 1 << 19;  // (10+ sec, at default 48k!)
  static constexpr uint32_t kDefaultModuloBufferFrames = 1;

  // By default, support a wide gain range with good precision.
  static constexpr audio::audio_proto::GetGainResp kDefaultGainState = {.cur_mute = false,
                                                                        .cur_agc = false,
                                                                        .cur_gain = -0.75f,
                                                                        .can_mute = true,
                                                                        .can_agc = false,
                                                                        .min_gain = -160.0f,
                                                                        .max_gain = 24.0f,
                                                                        .gain_step = 0.25f};

  // By default, device is hot-pluggable
  static constexpr bool kDefaultPlugged = true;
  static constexpr bool kDefaultHardwired = false;
  static constexpr bool kDefaultPlugCanNotify = true;

  static std::unique_ptr<VirtualAudioDeviceImpl> Create(VirtualAudioControlImpl* owner,
                                                        bool is_input);

  // Execute the given task on the FIDL channel's main dispatcher thread. Used to deliver callbacks
  // or events, from the driver execution domain.
  void PostToDispatcher(fit::closure task_to_post);

  void SetBinding(fidl::Binding<fuchsia::virtualaudio::Input,
                                std::unique_ptr<virtual_audio::VirtualAudioDeviceImpl>>* binding);
  void SetBinding(fidl::Binding<fuchsia::virtualaudio::Output,
                                std::unique_ptr<virtual_audio::VirtualAudioDeviceImpl>>* binding);

  virtual bool CreateStream(zx_device_t* devnode);
  void RemoveStream();
  void ClearStream();
  bool IsActive() { return (stream_ != nullptr); }

  void Init();

  //
  // virtualaudio.Configuration interface
  //
  void SetDeviceName(std::string device_name) override;
  void SetManufacturer(std::string manufacturer_name) override;
  void SetProduct(std::string product_name) override;
  void SetUniqueId(std::array<uint8_t, 16> unique_id) override;

  void AddFormatRange(uint32_t format_flags, uint32_t min_rate, uint32_t max_rate,
                      uint8_t min_chans, uint8_t max_chans, uint16_t rate_family_flags) override;
  void ClearFormatRanges() override;

  void SetClockProperties(int32_t clock_domain, int32_t initial_rate_adjustment_ppm) override;

  void SetFifoDepth(uint32_t fifo_depth_bytes) override;
  void SetExternalDelay(zx_duration_t external_delay) override;
  void SetRingBufferRestrictions(uint32_t min_frames, uint32_t max_frames,
                                 uint32_t modulo_frames) override;

  void SetGainProperties(float min_gain_db, float max_gain_db, float gain_step_db,
                         float current_gain_db, bool can_mute, bool current_mute, bool can_agc,
                         bool current_agc) override;

  void SetPlugProperties(zx_time_t plug_change_time, bool plugged, bool hardwired,
                         bool can_notify) override;

  void ResetConfiguration() override;

  //
  // virtualaudio.Device interface
  //
  void Add() override;
  void Remove() override;

  void GetFormat(fuchsia::virtualaudio::Device::GetFormatCallback callback) override;
  virtual void NotifySetFormat(uint32_t frames_per_second, uint32_t sample_format,
                               uint32_t num_channels, zx_duration_t external_delay);

  void GetGain(fuchsia::virtualaudio::Device::GetGainCallback callback) override;
  virtual void NotifySetGain(bool current_mute, bool current_agc, float current_gain_db);

  void GetBuffer(fuchsia::virtualaudio::Device::GetBufferCallback callback) override;
  virtual void NotifyBufferCreated(zx::vmo ring_buffer_vmo, uint32_t num_ring_buffer_frames,
                                   uint32_t notifications_per_ring);

  void SetNotificationFrequency(uint32_t notifications_per_ring) override;

  virtual void NotifyStart(zx_time_t start_time);
  virtual void NotifyStop(zx_time_t stop_time, uint32_t ring_buffer_position);

  void GetPosition(fuchsia::virtualaudio::Device::GetPositionCallback callback) override;
  virtual void NotifyPosition(zx_time_t monotonic_time, uint32_t ring_buffer_position);

  void ChangePlugState(zx_time_t plug_change_time, bool plugged) override;

  void AdjustClockRate(int32_t ppm_from_system_monotonic) override;

 private:
  friend class VirtualAudioStream;
  friend class std::default_delete<VirtualAudioDeviceImpl>;

  VirtualAudioDeviceImpl(VirtualAudioControlImpl* owner, bool is_input);
  virtual ~VirtualAudioDeviceImpl();

  void WarnActiveStreamNotAffected(const char* func_name);

  VirtualAudioControlImpl const* owner_;
  fbl::RefPtr<VirtualAudioStream> stream_;
  bool is_input_;

  // When the binding is closed, it is removed from the (ControlImpl-owned) BindingSet that contains
  // it, which in turn deletes the associated impl (since the binding holds a unique_ptr<impl>, not
  // an impl*). Something might get dispatched from other thread at around this time, so we enqueue
  // them (ClosureQueue) and use StopAndClear to cancel them during ~DeviceImpl (in RemoveStream).
  fidl::Binding<fuchsia::virtualaudio::Input,
                std::unique_ptr<virtual_audio::VirtualAudioDeviceImpl>>* input_binding_ = nullptr;
  fidl::Binding<fuchsia::virtualaudio::Output,
                std::unique_ptr<virtual_audio::VirtualAudioDeviceImpl>>* output_binding_ = nullptr;

  // Don't initialize here or in ctor; do it all in Init() so ResetConfiguration has same effect.
  std::string device_name_;
  std::string mfr_name_;
  std::string prod_name_;
  uint8_t unique_id_[16];

  int32_t clock_domain_;
  int32_t clock_rate_adjustment_;

  std::vector<audio_stream_format_range_t> supported_formats_;

  uint32_t fifo_depth_;
  zx_duration_t external_delay_nsec_;

  uint32_t min_buffer_frames_;
  uint32_t max_buffer_frames_;
  uint32_t modulo_buffer_frames_;

  audio::audio_proto::GetGainResp cur_gain_state_;

  zx_time_t plug_time_;
  bool plugged_;
  bool hardwired_;
  bool async_plug_notify_;

  bool override_notification_frequency_;
  uint32_t notifications_per_ring_;

  // The optional enables this to be emplaced when stream_ is created, minimizing memory churn.
  std::optional<ClosureQueue> task_queue_;
};

}  // namespace virtual_audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_
