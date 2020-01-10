// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_OUTPUT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_OUTPUT_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/zx/time.h>

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/audio_link.h"
#include "src/media/audio/audio_core/mix_stage.h"

namespace media::audio {

class RingBuffer;

class Packet;

class AudioOutput : public AudioDevice {
 public:
  ~AudioOutput() override = default;

  // Minimum clock lead time for this output
  zx::duration min_lead_time() const override { return min_lead_time_; }

 protected:
  friend class AudioOutputTest;

  AudioOutput(ThreadingModel* threading_model, DeviceRegistry* registry);

  void Process() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  fit::result<std::unique_ptr<Mixer>, zx_status_t> InitializeSourceLink(
      const AudioObject& source, fbl::RefPtr<Stream> stream) final;
  void CleanupSourceLink(const AudioObject& source, fbl::RefPtr<Stream> stream) final;

  void SetNextSchedTime(zx::time next_sched_time) {
    next_sched_time_ = next_sched_time;
    next_sched_time_known_ = true;
  }

  void SetupMixTask(const Format& format, size_t max_block_size_frames,
                    TimelineFunction device_reference_clock_to_output_frame)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) {
    FX_CHECK(format.sample_format() == fuchsia::media::AudioSampleFormat::FLOAT);
    mix_stage_ = std::make_unique<MixStage>(format, max_block_size_frames,
                                            device_reference_clock_to_output_frame);
  }

  void SetMinLeadTime(zx::duration min_lead_time) { min_lead_time_ = min_lead_time; }

  void Cleanup() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  virtual std::optional<MixStage::FrameSpan> StartMixJob(zx::time process_start)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;
  virtual void FinishMixJob(const MixStage::FrameSpan& span, float* buffer)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;

 private:
  // Timer used to schedule periodic mixing.
  void MixTimerThunk() {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &mix_domain());
    Process();
  }
  async::TaskClosureMethod<AudioOutput, &AudioOutput::MixTimerThunk> mix_timer_
      FXL_GUARDED_BY(mix_domain().token()){this};

  zx::duration min_lead_time_;
  zx::time next_sched_time_;
  bool next_sched_time_known_;

  std::unique_ptr<MixStage> mix_stage_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_OUTPUT_H_
