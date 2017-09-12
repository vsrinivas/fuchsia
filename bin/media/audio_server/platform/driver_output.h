// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/audio.h>
#include <zx/channel.h>
#include <zx/vmo.h>
#include <string>

#include "garnet/drivers/audio/dispatcher-pool/dispatcher-channel.h"
#include "garnet/bin/media/audio_server/platform/generic/standard_output_base.h"

namespace media {
namespace audio {

class DriverOutput : public StandardOutputBase {
 public:
  static fbl::RefPtr<AudioOutput> Create(zx::channel channel,
                                         AudioOutputManager* manager);
  ~DriverOutput();

  // AudioOutput implementation
  MediaResult Init() override;
  void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) override;

  void Cleanup() override;

  // StandardOutputBase implementation
  bool StartMixJob(MixJob* job, fxl::TimePoint process_start)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) override;
  bool FinishMixJob(const MixJob& job)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) override;

 private:
  enum class State {
    Uninitialized,
    WaitingToSetup,
    WaitingForSetFormatResponse,
    WaitingForRingBufferFifoDepth,
    WaitingForRingBufferVmo,
    Starting,
    Started,
    FatalError,
  };

  DriverOutput(AudioOutputManager* manager, zx::channel initial_stream_channel);
  void ScheduleNextLowWaterWakeup();

  // Dispatchers for messages received over stream and ring buffer channels, and
  // the channel closure handler.
  zx_status_t ReadMessage(
      const fbl::RefPtr<::audio::dispatcher::Channel>& channel,
      void* buf,
      uint32_t buf_size,
      uint32_t* bytes_read_out,
      zx::handle* handle_out)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  zx_status_t ProcessStreamChannelMessage()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  zx_status_t ProcessRingBufferChannelMessage()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void ProcessChannelClosed()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Stream channel message handlers.
  zx_status_t ProcessSetFormatResponse(
      const audio_stream_cmd_set_format_resp_t& resp,
      zx::channel rb_channel)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  zx_status_t ProcessPlugStateChange(bool plugged, zx_time_t plug_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Ring buffer message handlers.
  zx_status_t ProcessGetFifoDepthResponse(
      const audio_rb_cmd_get_fifo_depth_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  zx_status_t ProcessGetBufferResponse(
      const audio_rb_cmd_get_buffer_resp_t& resp,
      zx::vmo rb_vmo) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  zx_status_t ProcessStartResponse(const audio_rb_cmd_start_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // State machine timeout handler.
  zx_status_t OnCommandTimeout()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  State state_ = State::Uninitialized;
  fbl::RefPtr<::audio::dispatcher::Channel> stream_channel_;
  fbl::RefPtr<::audio::dispatcher::Channel> rb_channel_;
  fbl::RefPtr<::audio::dispatcher::Timer> cmd_timeout_;
  zx::channel initial_stream_channel_;
  zx::vmo rb_vmo_;
  uint64_t rb_size_ = 0;
  uint32_t rb_frames_ = 0;
  uint64_t rb_fifo_depth_ = 0;
  void* rb_virt_ = nullptr;

  uint32_t frames_per_sec_;
  uint16_t channel_count_;
  audio_sample_format_t sample_format_;
  uint32_t bytes_per_frame_;
  uint64_t start_ticks_;

  int64_t frames_sent_ = 0;
  uint32_t frames_to_mix_ = 0;
  int64_t fifo_frames_ = 0;
  int64_t low_water_frames_ = 0;
  zx_time_t underflow_start_time_ = 0;
  zx_time_t underflow_cooldown_deadline_ = 0;
  TimelineRate local_to_frames_;
  TimelineFunction local_to_output_;
};

}  // namespace audio
}  // namespace media
