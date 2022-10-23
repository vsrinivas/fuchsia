// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_STREAM_H_
#define SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_STREAM_H_

#include <fidl/fuchsia.virtualaudio/cpp/wire.h>
#include <lib/affine/transform.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zx/clock.h>
#include <lib/zx/result.h>

#include <deque>

#include <audio-proto/audio-proto.h>
#include <fbl/ref_ptr.h>

#include "src/media/audio/drivers/virtual_audio/virtual_audio_device_impl.h"

namespace virtual_audio {

class VirtualAudioStream : public audio::SimpleAudioStream {
 public:
  static fbl::RefPtr<VirtualAudioStream> Create(const VirtualAudioDeviceImpl::Config& cfg,
                                                std::weak_ptr<VirtualAudioDeviceImpl> owner,
                                                zx_device_t* dev_node);

  using ErrorT = fuchsia_virtualaudio::Error;

  // Expose so that PostToDispatcher callers can use this.
  using audio::SimpleAudioStream::domain_token;

  // Execute the given task on our dispatcher.
  // Typically callbacks should acquire domain_token() before calling any methods.
  void PostToDispatcher(fit::closure task_to_post);

  //
  // The following methods implement getters and setters for fuchsia.virtualaudio.Device.
  //

  struct CurrentFormat {
    uint32_t frames_per_second;
    uint32_t sample_format;
    uint32_t num_channels;
    zx::duration external_delay;
  };
  fit::result<ErrorT, CurrentFormat> GetFormatForVA() __TA_REQUIRES(domain_token());

  struct CurrentGain {
    bool mute;
    bool agc;
    float gain_db;
  };
  CurrentGain GetGainForVA() __TA_REQUIRES(domain_token());

  struct CurrentBuffer {
    zx::vmo vmo;
    uint32_t num_frames;
    uint32_t notifications_per_ring;
  };
  fit::result<ErrorT, CurrentBuffer> GetBufferForVA() __TA_REQUIRES(domain_token());

  struct CurrentPosition {
    zx::time monotonic_time;
    uint32_t ring_position;
  };
  fit::result<ErrorT, CurrentPosition> GetPositionForVA() __TA_REQUIRES(domain_token());

  void SetNotificationFrequencyFromVA(uint32_t notifications_per_ring)
      __TA_REQUIRES(domain_token());
  void ChangePlugStateFromVA(bool plugged) __TA_REQUIRES(domain_token());
  void AdjustClockRateFromVA(int32_t ppm_from_monotonic) __TA_REQUIRES(domain_token());

 private:
  friend class audio::SimpleAudioStream;
  friend class fbl::RefPtr<VirtualAudioStream>;

  VirtualAudioStream(const VirtualAudioDeviceImpl::Config& cfg,
                     std::weak_ptr<VirtualAudioDeviceImpl> parent, zx_device_t* dev_node)
      : audio::SimpleAudioStream(dev_node, cfg.is_input),
        config_(cfg),
        parent_(std::move(parent)) {}

  //
  // Implementation of audio::SimpleAudioStream.
  //

  zx_status_t Init() __TA_REQUIRES(domain_token()) override;
  zx_status_t EstablishReferenceClock() __TA_REQUIRES(domain_token());
  zx_status_t AdjustClockRate() __TA_REQUIRES(domain_token());

  zx_status_t ChangeFormat(const audio::audio_proto::StreamSetFmtReq& req)
      __TA_REQUIRES(domain_token()) override;
  zx_status_t SetGain(const audio::audio_proto::SetGainReq& req)
      __TA_REQUIRES(domain_token()) override;

  zx_status_t GetBuffer(const audio::audio_proto::RingBufGetBufferReq& req,
                        uint32_t* out_num_rb_frames, zx::vmo* out_buffer)
      __TA_REQUIRES(domain_token()) override;

  zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_token()) override;
  zx_status_t Stop() __TA_REQUIRES(domain_token()) override;
  zx_status_t ChangeActiveChannels(uint64_t mask) __TA_REQUIRES(domain_token()) override {
    return ZX_ERR_NOT_SUPPORTED;  // TODO(81649): Add support.
  }

  void ShutdownHook() __TA_REQUIRES(domain_token()) override;
  // RingBufferShutdown() is unneeded: no hardware shutdown tasks needed...

  void ProcessRingNotification();
  void SetNotificationPeriods() __TA_REQUIRES(domain_token());
  void PostForNotify() __TA_REQUIRES(domain_token());
  void PostForNotifyAt(zx::time ref_notification_time) __TA_REQUIRES(domain_token());

  void ProcessVaClientRingNotification();
  void SetVaClientNotificationPeriods() __TA_REQUIRES(domain_token());
  void PostForVaClientNotify() __TA_REQUIRES(domain_token());
  void PostForVaClientNotifyAt(zx::time ref_notification_time) __TA_REQUIRES(domain_token());

  //
  // Other methods
  //

  static zx::time MonoTimeFromRefTime(const zx::clock& clock, zx::time ref_time);

  const VirtualAudioDeviceImpl::Config config_;

  // This should never be invalid: this VirtualAudioStream should always be destroyed before
  // its parent. This field is a weak_ptr to avoid a circular reference count.
  const std::weak_ptr<VirtualAudioDeviceImpl> parent_;

  // Accessed in GetBuffer, defended by token.
  fzl::VmoMapper ring_buffer_mapper_ __TA_GUARDED(domain_token());
  zx::vmo ring_buffer_vmo_ __TA_GUARDED(domain_token());
  uint32_t num_ring_buffer_frames_ __TA_GUARDED(domain_token()) = 0;

  uint32_t max_buffer_frames_ __TA_GUARDED(domain_token());
  uint32_t min_buffer_frames_ __TA_GUARDED(domain_token());
  uint32_t modulo_buffer_frames_ __TA_GUARDED(domain_token());

  zx::time ref_start_time_ __TA_GUARDED(domain_token());

  // Members related to the driver's delivery of position notifications to AudioCore.
  async::TaskClosureMethod<VirtualAudioStream, &VirtualAudioStream::ProcessRingNotification>
      notify_timer_ __TA_GUARDED(domain_token()){this};
  uint32_t notifications_per_ring_ __TA_GUARDED(domain_token()) = 0;
  zx::duration ref_notification_period_ __TA_GUARDED(domain_token()) = zx::duration(0);
  zx::time target_mono_notification_time_ __TA_GUARDED(domain_token()) = zx::time(0);
  zx::time target_ref_notification_time_ __TA_GUARDED(domain_token()) = zx::time(0);

  // Members related to driver delivery of position notifications to a VirtualAudio client, with an
  // alternate notifications-per-ring cadence. If a VirtualAudio client specifies the same cadence
  // that AudioCore has requested, then we simply use the above members and deliver those same
  // notifications to the VA client as well.
  async::TaskClosureMethod<VirtualAudioStream, &VirtualAudioStream::ProcessVaClientRingNotification>
      va_client_notify_timer_ __TA_GUARDED(domain_token()){this};

  std::optional<uint32_t> va_client_notifications_per_ring_ __TA_GUARDED(domain_token()) =
      std::nullopt;
  zx::duration va_client_ref_notification_period_ __TA_GUARDED(domain_token()) = zx::duration(0);
  zx::time target_va_client_mono_notification_time_ __TA_GUARDED(domain_token()) = zx::time(0);
  zx::time target_va_client_ref_notification_time_ __TA_GUARDED(domain_token()) = zx::time(0);

  uint32_t bytes_per_sec_ __TA_GUARDED(domain_token()) = 0;
  uint32_t frame_rate_ __TA_GUARDED(domain_token()) = 0;
  audio_sample_format_t sample_format_ __TA_GUARDED(domain_token()) = 0;
  uint32_t num_channels_ __TA_GUARDED(domain_token()) = 0;
  affine::Transform ref_time_to_running_frame_ __TA_GUARDED(domain_token());

  zx::clock reference_clock_ __TA_GUARDED(domain_token());
  int32_t clock_rate_adjustment_ __TA_GUARDED(domain_token());
};

}  // namespace virtual_audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_STREAM_H_
