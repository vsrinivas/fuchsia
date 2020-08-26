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
#include "src/media/audio/lib/wav/wav_writer.h"

namespace media::audio {

constexpr bool kEnableFinalMixWavWriter = false;

class DriverOutput : public AudioOutput {
 public:
  // TODO(13550): Revert these to 20/30 instead of 50/60. In the long term, get these into the
  // range of 5/10.
  static constexpr zx::duration kDefaultLowWaterNsec = zx::msec(50);
  static constexpr zx::duration kDefaultHighWaterNsec = zx::msec(60);

  DriverOutput(const std::string& name, ThreadingModel* threading_model, DeviceRegistry* registry,
               zx::channel initial_stream_channel, LinkMatrix* link_matrix,
               VolumeCurve volume_curve);
  DriverOutput(const std::string& name, ThreadingModel* threading_model, DeviceRegistry* registry,
               fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> channel,
               LinkMatrix* link_matrix, VolumeCurve volume_curve);

  ~DriverOutput();

  const PipelineConfig& pipeline_config() const;

 protected:
  // AudioOutput implementation
  zx_status_t Init() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;
  void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;
  std::optional<AudioOutput::FrameSpan> StartMixJob(zx::time ref_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override;
  void FinishMixJob(const AudioOutput::FrameSpan& span, float* buffer)
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
  void WriteToRing(const AudioOutput::FrameSpan& span,
                   fit::function<void(uint64_t offset, uint32_t length, void* dest_buf)> writer)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());
  void FillRingSpanWithSilence(const AudioOutput::FrameSpan& span)
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
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_DRIVER_OUTPUT_H_
