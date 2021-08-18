// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_

#include <lib/zx/channel.h>
#include <lib/zx/time.h>

#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/channel_attributes.h"
#include "src/media/audio/audio_core/mixer/output_producer.h"
#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/lib/wav/wav_writer.h"

namespace media::audio {

constexpr bool kEnableFinalMixWavWriter = false;

class DriverOutput : public AudioOutput {
 public:
  // AudioCore supplies data to audio output devices periodically; when doing so it must stay
  // safely ahead of the hardware (without adding excessive latency).
  //
  // DriverOutput knows where the audio hardware is currently reading in the ring buffer. It sets a
  // timer to awaken when the amount of unread audio reaches the "low-water" amount, then requests
  // enough mixed data from its upstream pipeline to fill the ring buffer to the "high-water" level.
  // Because it can take as long as an entire mix profile period for the thread to be scheduled and
  // mix the needed audio into the ring buffer, kDefaultLowWaterNsec is equal to kMixProfilePeriod.
  //
  // The output pipeline's total latency will currently be 20 ms + fifo depth + external delay.
  static constexpr zx::duration kDefaultLowWaterNsec = ThreadingModel::kMixProfilePeriod;
  static constexpr zx::duration kDefaultHighWaterNsec =
      kDefaultLowWaterNsec + ThreadingModel::kMixProfilePeriod;

  DriverOutput(const std::string& name, ThreadingModel* threading_model, DeviceRegistry* registry,
               fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> channel,
               LinkMatrix* link_matrix, std::shared_ptr<AudioClockFactory> clock_factory,
               VolumeCurve volume_curve);

  ~DriverOutput();

  const PipelineConfig* pipeline_config() const override;

  zx_status_t EnableAudible() override;
  zx_status_t EnableUltrasonic() override;
  zx_status_t StartCountdownToDisableAudible(zx::duration countdown) override;
  zx_status_t StartCountdownToDisableUltrasonic(zx::duration countdown) override;

 protected:
  // AudioOutput implementation
  zx_status_t Init() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;
  void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;
  std::optional<AudioOutput::FrameSpan> StartMixJob(zx::time ref_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;
  void FinishMixJob(const AudioOutput::FrameSpan& span, const float* buffer)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;

  zx::duration MixDeadline() const override { return kDefaultHighWaterNsec - kDefaultLowWaterNsec; }

  // AudioDevice implementation
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                       fuchsia::media::AudioGainValidFlags set_flags) override;

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

  int64_t RefTimeToSafeWriteFrame(zx::time ref_time) const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  zx::time SafeWriteFrameToRefTime(int64_t frame) const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  TimelineRate FramesPerRefTick() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void ScheduleNextLowWaterWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Callbacks triggered by our driver object as it completes various
  // asynchronous tasks.
  void OnDriverInfoFetched() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverConfigComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverStartComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Uses |writer| to populate the frames specified by |span|.
  //
  // Writer will be called iteratively with a |offset| frame, a |length| (also in frames), and
  // a |dest_buf|, which is the pointer into the ring buffer for the frame |start|.
  //
  // Note: here |offset| is relative to |span.start|. The absolute frame for the write is simply
  // |span.start + offset|.
  void WriteToRing(const AudioOutput::FrameSpan& span, const float* buffer)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  State state_ = State::Uninitialized;
  zx::channel initial_stream_channel_;
  VolumeCurve volume_curve_;

  int64_t frames_sent_ = 0;
  int64_t low_water_frames_ = 0;
  zx::time underflow_start_time_mono_;
  zx::time underflow_cooldown_deadline_mono_;

  // Details about the final output format
  std::unique_ptr<OutputProducer> output_producer_;

  // This atomic is only used when the final-mix wave-writer is enabled --
  // specifically to generate unique ids for each final-mix WAV file.
  static std::atomic<uint32_t> final_mix_instance_num_;
  WavWriter<kEnableFinalMixWavWriter> wav_writer_;

  void CountdownExpiredAudible();
  void CountdownExpiredUltrasonic();
  zx_status_t CancelCountdownAudible();
  zx_status_t CancelCountdownUltrasonic();
  uint64_t UpdateActiveChannels() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  async::TaskClosureMethod<DriverOutput, &DriverOutput::CountdownExpiredAudible> audible_countdown_{
      this};
  async::TaskClosureMethod<DriverOutput, &DriverOutput::CountdownExpiredUltrasonic>
      ultrasonic_countdown_{this};

  // Set only once during OnDriverConfigComplete, subsequently readable from arbitrary context
  bool supports_audible_;
  bool supports_ultrasonic_;

  bool supports_set_active_channels_ FXL_GUARDED_BY(mix_domain().token()) = true;
  bool audible_enabled_ FXL_GUARDED_BY(mix_domain().token());
  bool ultrasonic_enabled_ FXL_GUARDED_BY(mix_domain().token());

  std::vector<ChannelAttributes> channel_config_;
  uint64_t current_active_channel_mask_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_
