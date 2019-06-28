// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_

#include <lib/zx/channel.h>

#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/lib/wav_writer/wav_writer.h"

namespace media::audio {

constexpr bool kEnableFinalMixWavWriter = false;

class DriverOutput : public AudioOutput {
 public:
  static fbl::RefPtr<AudioOutput> Create(zx::channel channel, AudioDeviceManager* manager);
  ~DriverOutput();

 protected:
  // AudioOutput implementation
  zx_status_t Init() override;
  void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) override;
  bool StartMixJob(MixJob* job, fxl::TimePoint process_start)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) override;
  bool FinishMixJob(const MixJob& job) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) override;

  // AudioDevice implementation
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) override;

 private:
  enum class State {
    Uninitialized,
    FormatsUnknown,
    FetchingFormats,
    Configuring,
    Starting,
    Started,
    Shutdown,
  };

  DriverOutput(AudioDeviceManager* manager, zx::channel initial_stream_channel);
  void ScheduleNextLowWaterWakeup();

  // Callbacks triggered by our driver object as it completes various
  // asynchronous tasks.
  void OnDriverInfoFetched() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  void OnDriverConfigComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  void OnDriverStartComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  void OnDriverPlugStateChange(bool plugged, zx_time_t plug_time) override
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  State state_ = State::Uninitialized;
  zx::channel initial_stream_channel_;

  int64_t frames_sent_ = 0;
  uint32_t frames_to_mix_ = 0;
  int64_t low_water_frames_ = 0;
  TimelineFunction clock_mono_to_ring_buf_pos_frames_;
  GenerationId clock_mono_to_ring_buf_pos_id_;
  zx_time_t underflow_start_time_ = 0;
  zx_time_t underflow_cooldown_deadline_ = 0;

  // This atomic is only used when the final-mix wave-writer is enabled --
  // specifically to generate unique ids for each final-mix WAV file.
  static std::atomic<uint32_t> final_mix_instance_num_;
  WavWriter<kEnableFinalMixWavWriter> wav_writer_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_
