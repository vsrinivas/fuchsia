// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DRIVER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DRIVER_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fpromise/result.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/device/audio.h>

#include <cstring>
#include <optional>

namespace media::audio::testing {

class FakeAudioDriver : public fuchsia::hardware::audio::StreamConfig,
                        public fuchsia::hardware::audio::RingBuffer {
 public:
  FakeAudioDriver(zx::channel channel, async_dispatcher_t* dispatcher);

  fzl::VmoMapper CreateRingBuffer(size_t size);
  void Start();
  void Stop();

  void set_stream_unique_id(const audio_stream_unique_id_t& uid) {
    std::memcpy(uid_.data, uid.data, sizeof(uid.data));
  }
  void set_device_manufacturer(std::string mfgr) { manufacturer_ = std::move(mfgr); }
  void set_device_product(std::string product) { product_ = std::move(product); }
  void set_gain(float gain) { cur_gain_ = gain; }
  void set_gain_limits(float min_gain, float max_gain) {
    gain_limits_ = std::make_pair(min_gain, max_gain);
  }
  void set_can_agc(bool can_agc) { can_agc_ = can_agc; }
  void set_cur_agc(bool cur_agc) { cur_agc_ = cur_agc; }
  void set_can_mute(bool can_mute) { can_mute_ = can_mute; }
  void set_cur_mute(bool cur_mute) { cur_mute_ = cur_mute; }
  void set_formats(fuchsia::hardware::audio::PcmSupportedFormats formats) {
    formats_ = std::move(formats);
  }
  void set_clock_domain(uint32_t clock_domain) { clock_domain_ = clock_domain; }
  void set_plugged(bool plugged) { plugged_ = plugged; }
  void set_fifo_depth(uint32_t fifo_depth) { fifo_depth_ = fifo_depth; }
  void set_external_delay(zx::duration external_delay) { external_delay_ = external_delay; }

  void clear_fifo_depth() { fifo_depth_ = std::nullopt; }
  void clear_external_delay() { external_delay_ = std::nullopt; }

  void SendPositionNotification(zx::time timestamp, uint32_t position);

  // |true| after an |audio_rb_cmd_start| is received, until an |audio_rb_cmd_stop| is received.
  bool is_running() const { return is_running_; }
  zx::time mono_start_time() const { return mono_start_time_; }

  // The 'selected format' for the driver.
  // The returned optional will be empty if no |CreateRingBuffer| command has been received.
  std::optional<fuchsia::hardware::audio::PcmFormat> selected_format() const {
    return selected_format_;
  }

 private:
  // fuchsia hardware audio StreamConfig Interface
  void GetProperties(fuchsia::hardware::audio::StreamConfig::GetPropertiesCallback callback) final;
  void GetHealthState(
      fuchsia::hardware::audio::StreamConfig::GetHealthStateCallback callback) override {
    callback({});
  }
  void SignalProcessingConnect(
      fidl::InterfaceRequest<fuchsia::hardware::audio::signalprocessing::SignalProcessing>
          signal_processing) override {
    signal_processing.Close(ZX_ERR_NOT_SUPPORTED);
  }
  void GetSupportedFormats(
      fuchsia::hardware::audio::StreamConfig::GetSupportedFormatsCallback callback) final;
  void CreateRingBuffer(
      fuchsia::hardware::audio::Format format,
      ::fidl::InterfaceRequest<fuchsia::hardware::audio::RingBuffer> ring_buffer) final;
  void WatchGainState(
      fuchsia::hardware::audio::StreamConfig::WatchGainStateCallback callback) final;
  void SetGain(fuchsia::hardware::audio::GainState target_state) final;
  void WatchPlugState(
      fuchsia::hardware::audio::StreamConfig::WatchPlugStateCallback callback) final;

  // fuchsia hardware audio RingBuffer Interface
  void GetProperties(fuchsia::hardware::audio::RingBuffer::GetPropertiesCallback callback) final;
  void WatchClockRecoveryPositionInfo(
      fuchsia::hardware::audio::RingBuffer::WatchClockRecoveryPositionInfoCallback callback) final;
  void GetVmo(uint32_t min_frames, uint32_t clock_recovery_notifications_per_ring,
              fuchsia::hardware::audio::RingBuffer::GetVmoCallback callback) final;
  void Start(fuchsia::hardware::audio::RingBuffer::StartCallback callback) final;
  void Stop(fuchsia::hardware::audio::RingBuffer::StopCallback callback) final;
  void SetActiveChannels(
      uint64_t active_channels_bitmask,
      fuchsia::hardware::audio::RingBuffer::SetActiveChannelsCallback callback) override {
    callback(fpromise::error(ZX_ERR_NOT_SUPPORTED));
  }
  void WatchDelayInfo(fuchsia::hardware::audio::RingBuffer::WatchDelayInfoCallback callback) final;

  void PositionNotification();

  audio_stream_unique_id_t uid_ = {};
  std::string manufacturer_ = "default manufacturer";
  std::string product_ = "default product";
  float cur_gain_ = 0.0f;
  std::pair<float, float> gain_limits_{-160.0f, 3.0f};
  bool can_agc_ = true;
  bool cur_agc_ = false;
  bool can_mute_ = true;
  bool cur_mute_ = false;
  bool plug_state_sent_ = false;
  bool gain_state_sent_ = false;
  bool delay_info_sent_ = false;
  fuchsia::hardware::audio::PcmSupportedFormats formats_ = {};
  uint32_t clock_domain_ = fuchsia::hardware::audio::CLOCK_DOMAIN_MONOTONIC;
  size_t ring_buffer_size_;
  zx::vmo ring_buffer_;

  std::optional<uint32_t> fifo_depth_;
  std::optional<zx::duration> external_delay_;
  bool plugged_ = true;

  std::optional<fuchsia::hardware::audio::PcmFormat> selected_format_;

  bool is_running_ = false;
  zx::time mono_start_time_{0};

  async_dispatcher_t* dispatcher_;
  fidl::Binding<fuchsia::hardware::audio::StreamConfig> stream_binding_;
  std::optional<fidl::Binding<fuchsia::hardware::audio::RingBuffer>> ring_buffer_binding_;
  fidl::InterfaceRequest<fuchsia::hardware::audio::StreamConfig> stream_req_;
  fidl::InterfaceRequest<fuchsia::hardware::audio::RingBuffer> ring_buffer_req_;

  bool position_notification_values_are_set_ = false;
  zx::time position_notify_timestamp_mono_;
  uint32_t position_notify_position_bytes_ = 0;

  std::optional<fuchsia::hardware::audio::RingBuffer::WatchClockRecoveryPositionInfoCallback>
      position_notify_callback_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_AUDIO_DRIVER_H_
