// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_OUTPUT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_OUTPUT_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/zx/time.h>

#include <optional>

#include "src/media/audio/audio_core/shared/device_config.h"
#include "src/media/audio/audio_core/shared/process_config.h"
#include "src/media/audio/audio_core/v1/audio_device.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/output_pipeline.h"
#include "src/media/audio/audio_core/v1/reporter.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

class Packet;
class EffectsLoaderV2;

class AudioOutput : public AudioDevice {
 public:
  ~AudioOutput() override = default;

  fpromise::promise<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config) override;

  // Replace the existing DeviceProfile and restart the OutputPipeline, for tuning purposes.
  fpromise::promise<void, zx_status_t> UpdateDeviceProfile(
      const DeviceConfig::OutputDeviceProfile::Parameters& params) override;

  OutputPipeline* output_pipeline() const { return pipeline_.get(); }

  // |media::audio::AudioDevice|
  void SetGainInfo(const fuchsia::media::AudioGainInfo& info,
                   fuchsia::media::AudioGainValidFlags set_flags) override;

 protected:
  AudioOutput(const std::string& name, const DeviceConfig& config, ThreadingModel* threading_model,
              DeviceRegistry* registry, LinkMatrix* link_matrix,
              std::shared_ptr<AudioCoreClockFactory> clock_factory,
              EffectsLoaderV2* effects_loader_v2, std::unique_ptr<AudioDriver>);

  Reporter::OutputDevice& reporter() { return *reporter_; }
  EffectsLoaderV2* effects_loader_v2() const { return effects_loader_v2_; }

  // |media::audio::AudioObject|
  //
  // If we're initializing a source link, then we're connecting a renderer to this output. Else
  // if we're initializing a dest link, then we're being connected as a loopback so we should return
  // our loopback stream.
  fpromise::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
  InitializeSourceLink(const AudioObject& source, std::shared_ptr<ReadableStream> stream) final;
  void CleanupSourceLink(const AudioObject& source, std::shared_ptr<ReadableStream> stream) final;
  fpromise::result<std::shared_ptr<ReadableStream>, zx_status_t> InitializeDestLink(
      const AudioObject& dest) override;

  // Mark this output as needing to be mixed at the specified future time.
  // async PostForTime requires a time in the CLOCK_MONOTONIC timebase, so we use that here.
  void SetNextSchedTimeMono(zx::time next_sched_time_mono) {
    next_sched_time_mono_ = next_sched_time_mono;
  }

  inline void ClearNextSchedTime() { next_sched_time_mono_ = std::nullopt; }

  void SetupMixTask(const DeviceConfig::OutputDeviceProfile& profile, size_t max_block_size_frames,
                    TimelineFunction device_reference_clock_to_fractional_frame)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());
  virtual std::shared_ptr<OutputPipeline> CreateOutputPipeline(
      const PipelineConfig& config, const VolumeCurve& volume_curve, size_t max_block_size_frames,
      TimelineFunction device_reference_clock_to_fractional_frame,
      std::shared_ptr<Clock> ref_clock);

  void Cleanup() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  struct FrameSpan {
    int64_t start;
    int64_t length;
    bool is_mute;
  };

  void Process() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());
  void ProcessMixJob(ReadableStream::ReadLockContext& ctx, FrameSpan mix_span)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Start mixing frames for a periodic mix job. This is called internally during the periodic mix
  // task for this output. Implementations can control mix behavior in the following ways:
  //
  // If |std::nullopt| is returned, then no frames will be mixed. Instead all inputs will be trimmed
  // such that any client audio packets that would have been fully consumed by the end of this mix
  // job will be released. There will be no call to |FinishMixJob|.
  //
  // If the returned optional contains a FrameSpan with |is_mute| set to true, then no frames will
  // be mixed. Instead all inputs will be trimmed such that any client audio packets that would have
  // been fully consumed by the end of this mix job will be released. |WriteMixOutput| will be
  // called to write silence. |FinishMixJob| will be called with the returned FrameSpan.
  //
  // If the returned optional contains a FrameSpan with |is_mute| set to false, then the mix
  // pipeline will be advanced by the requested frame region. |WriteMixOutput| will be called one
  // or more times to write the mixed output. |FinishMixJob| will be called with the returned
  // FrameSpan.
  virtual std::optional<AudioOutput::FrameSpan> StartMixJob(zx::time device_ref_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;

  // Writes frames in the given region. The start and end will intersect the FrameSpan returned from
  // a prior |StartMixJob|. Write the given |payload|, or silence if |payload| is nullptr.
  virtual void WriteMixOutput(int64_t start, int64_t length, const float* payload)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;

  // This is called at the end of a mix job to update internal state. |span| is the same span
  // returned by the last call to StartMixJob.
  virtual void FinishMixJob(const AudioOutput::FrameSpan& span)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;

  // The maximum amount of time it can take to run all pending mix jobs when a device
  // wakes up to process pending jobs.
  virtual zx::duration MixDeadline() const FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;

 private:
  // Timer used to schedule periodic mixing.
  void MixTimerThunk() {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &mix_domain());
    Process();
  }
  async::TaskClosureMethod<AudioOutput, &AudioOutput::MixTimerThunk> mix_timer_
      FXL_GUARDED_BY(mix_domain().token()){this};

  std::optional<zx::time> next_sched_time_mono_;
  size_t max_block_size_frames_;

  std::shared_ptr<OutputPipeline> pipeline_;
  Reporter::Container<Reporter::OutputDevice, Reporter::kObjectsToCache>::Ptr reporter_;
  EffectsLoaderV2* effects_loader_v2_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_AUDIO_OUTPUT_H_
