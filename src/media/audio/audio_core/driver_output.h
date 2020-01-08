// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_

#include <lib/zx/channel.h>
#include <lib/zx/time.h>

#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/mixer/output_producer.h"
#include "src/media/audio/lib/wav_writer/wav_writer.h"

namespace media::audio {

constexpr bool kEnableFinalMixWavWriter = false;

class DriverOutput : public AudioOutput {
 public:
  // TODO(13550): Revert these to 20/30 instead of 50/60. In the long term, get these into the
  // range of 5/10.
  static constexpr zx::duration kDefaultLowWaterNsec = zx::msec(50);
  static constexpr zx::duration kDefaultHighWaterNsec = zx::msec(60);

  static fbl::RefPtr<AudioOutput> Create(zx::channel channel, ThreadingModel* threading_model,
                                         DeviceRegistry* registry);
  ~DriverOutput();

 protected:
  // AudioOutput implementation
  zx_status_t Init() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;
  void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;
  std::optional<MixStage::FrameSpan> StartMixJob(zx::time process_start)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;
  void FinishMixJob(const MixStage::FrameSpan& span, float* buffer)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;

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

  DriverOutput(ThreadingModel* threading_model, DeviceRegistry* registry,
               zx::channel initial_stream_channel);
  void ScheduleNextLowWaterWakeup();

  // Callbacks triggered by our driver object as it completes various
  // asynchronous tasks.
  void OnDriverInfoFetched() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverConfigComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverStartComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverPlugStateChange(bool plugged, zx::time plug_time) override
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  State state_ = State::Uninitialized;
  zx::channel initial_stream_channel_;

  int64_t frames_sent_ = 0;
  int64_t low_water_frames_ = 0;
  TimelineFunction clock_monotonic_to_output_frame_;
  GenerationId clock_monotonic_to_output_frame_generation_;
  zx::time underflow_start_time_;
  zx::time underflow_cooldown_deadline_;

  // Details about the final output format
  std::unique_ptr<OutputProducer> output_producer_;

  // This atomic is only used when the final-mix wave-writer is enabled --
  // specifically to generate unique ids for each final-mix WAV file.
  static std::atomic<uint32_t> final_mix_instance_num_;
  WavWriter<kEnableFinalMixWavWriter> wav_writer_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_
