// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_
#define SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_

#include <fidl/fuchsia.virtualaudio/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/device.h>
#include <lib/fit/result.h>
#include <lib/zx/clock.h>

#include <memory>
#include <unordered_set>

#include <audio-proto/audio-proto.h>
#include <fbl/ref_ptr.h>

namespace virtual_audio {

class VirtualAudioStream;

// Controller for a `VirtualAudioStream`.
//
// Each instance of this class represents a two objects:
//
// 1. A virtual audio device in the device tree, represented by a `VirtualAudioStream` object.
//    This device appears under `/dev/class/audio-{input,output}`.
//
// 2. A FIDL channel (`fuchsia.virtualaudio.Device`) which controls and monitors the device.
//
// The device lives until the controlling FIDL channel is closed or the device host process decides
// to remove the `VirtualAudioStream`.
class VirtualAudioDeviceImpl : public fidl::WireServer<fuchsia_virtualaudio::Device>,
                               public std::enable_shared_from_this<VirtualAudioDeviceImpl> {
 public:
  struct Config {
    bool is_input;

    std::string device_name;
    std::string manufacturer_name;
    std::string product_name;
    std::array<uint8_t, 16> unique_id;

    uint32_t fifo_depth_bytes;
    zx::duration external_delay;
    std::vector<audio_stream_format_range_t> supported_formats;

    fuchsia_virtualaudio::wire::ClockProperties clock;
    fuchsia_virtualaudio::wire::RingBufferConstraints ring_buffer;
    fuchsia_virtualaudio::wire::GainProperties gain;
    fuchsia_virtualaudio::wire::PlugProperties plug;
    std::optional<uint32_t> initial_notifications_per_ring;
  };

  static fit::result<fuchsia_virtualaudio::Error, std::shared_ptr<VirtualAudioDeviceImpl>> Create(
      const Config& cfg, fidl::ServerEnd<fuchsia_virtualaudio::Device> server,
      zx_device_t* dev_node, async_dispatcher_t* fidl_dispatcher);

  bool is_input() const { return is_input_; }

  // Executes the given task on the FIDL channel's main dispatcher thread.
  // Used to deliver callbacks or events from the driver execution domain.
  void PostToDispatcher(fit::closure task_to_post);

  // Shuts down this server.
  // Shutdown happens asynchronously, after which `cb` is called from the PostToDispatcher thread.
  // Must be called from the PostToDispatcher thread.
  void ShutdownAsync(fit::closure cb);

  // `VirtualAudioStream` uses this to tell us that the stream has been shut down by an external
  // entity (such as the device host process). Must be called from a PostToDispatcher closure.
  void StreamIsShuttingDown();

  //
  // Implementation of virtualaudio.Device.
  // Event triggers (e.g. NotifySetFormat) may be called from any thread.
  //

  void GetFormat(GetFormatCompleter::Sync& completer) override;
  void NotifySetFormat(uint32_t frames_per_second, uint32_t sample_format, uint32_t num_channels,
                       zx_duration_t external_delay);

  void GetGain(GetGainCompleter::Sync& completer) override;
  void NotifySetGain(bool current_mute, bool current_agc, float current_gain_db);

  void GetBuffer(GetBufferCompleter::Sync& completer) override;
  void NotifyBufferCreated(zx::vmo ring_buffer_vmo, uint32_t num_ring_buffer_frames,
                           uint32_t notifications_per_ring);

  void SetNotificationFrequency(SetNotificationFrequencyRequestView request,
                                SetNotificationFrequencyCompleter::Sync& completer) override;

  void NotifyStart(zx_time_t start_time);
  void NotifyStop(zx_time_t stop_time, uint32_t ring_buffer_position);

  void GetPosition(GetPositionCompleter::Sync& completer) override;
  void NotifyPosition(zx_time_t monotonic_time, uint32_t ring_buffer_position);

  void ChangePlugState(ChangePlugStateRequestView request,
                       ChangePlugStateCompleter::Sync& completer) override;

  void AdjustClockRate(AdjustClockRateRequestView request,
                       AdjustClockRateCompleter::Sync& completer) override;

  // Public for std::make_shared. Use Create, not this ctor.
  VirtualAudioDeviceImpl(bool is_input, async_dispatcher_t* fidl_dispatcher);
  ~VirtualAudioDeviceImpl() override;

 private:
  const bool is_input_;
  async_dispatcher_t* const fidl_dispatcher_;

  // This is std::optional only to break a circular dependency. In practice this is set during
  // Create() then never changed, so during normal operation is should never be std::nullopt.
  std::optional<fidl::ServerBindingRef<fuchsia_virtualaudio::Device>> binding_;
  bool is_bound_ = true;  // starts bound after Create

  // This may be nullptr if the underlying stream device is removed before the
  // fuchsia.virtualaudio.Device FIDL channel is closed.
  fbl::RefPtr<VirtualAudioStream> stream_;

  // Callbacks to run on destroy.
  std::vector<fit::closure> on_destroy_callbacks_;
};

}  // namespace virtual_audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_
