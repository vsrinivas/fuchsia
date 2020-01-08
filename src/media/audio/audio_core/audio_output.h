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

  zx_status_t InitializeSourceLink(const fbl::RefPtr<AudioLink>& link) final;

  void SetNextSchedTime(zx::time next_sched_time) {
    next_sched_time_ = next_sched_time;
    next_sched_time_known_ = true;
  }

  void SetupMixTask(const Format& format, size_t max_block_size_frames)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) {
    FX_CHECK(format.sample_format() == fuchsia::media::AudioSampleFormat::FLOAT);
    mix_format_ = {format};
    SetupMixBuffer(max_block_size_frames);
  }

  void SetMinLeadTime(zx::duration min_lead_time) { min_lead_time_ = min_lead_time; }

  void Cleanup() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  struct FrameSpan {
    int64_t start;
    size_t length;

    // If |true| then the output is muted and no mix is needed for these frames, but the mix loop
    // can consume and release any packets that cover this span.
    bool muted;

    // A mapping between reference clock to frame number. If the same
    // |reference_clock_to_destination_frame_generation| is returned between consecutive calls to
    // |StartMixJob|, then the |reference_clock_to_frame| function is guaranteed to be the same.
    TimelineFunction reference_clock_to_frame;
    uint32_t reference_clock_to_destination_frame_generation;
  };
  virtual std::optional<FrameSpan> StartMixJob(zx::time process_start)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;
  virtual void FinishMixJob(const FrameSpan& span, float* buffer)
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

  void SetupMixBuffer(uint32_t max_mix_frames) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  struct MixJob {
    // Job state set up once by an output implementation, used by all renderers.
    float* buf;
    uint32_t buf_frames;
    int64_t start_pts_of;  // start PTS, expressed in output frames.
    uint32_t reference_clock_to_destination_frame_gen;
    bool accumulate;
    const TimelineFunction* reference_clock_to_destination_frame;

    bool sw_output_muted;

    // Per-stream job state, set up for each renderer during SetupMix.
    uint32_t frames_produced;
  };

  // TODO(13415): Integrate it into the Mixer class itself.
  void UpdateSourceTrans(const Stream& stream, Mixer::Bookkeeping* bk);
  void UpdateDestTrans(const MixJob& job, Mixer::Bookkeeping* bk);

  enum class TaskType { Mix, Trim };

  void ForEachSource(TaskType task_type, zx::time ref_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void SetupMix(Mixer* mixer) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());
  bool ProcessMix(Stream* stream, Mixer* mixer, const Stream::Buffer& buffer)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void SetupTrim(Mixer* mixer, zx::time trim_point)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());
  bool ProcessTrim(const Stream::Buffer& buffer) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Vector used to hold references to source links while mixing (instead of
  // holding a lock, preventing source_links_ mutation for the entire mix job).
  std::vector<fbl::RefPtr<AudioLink>> source_link_refs_ FXL_GUARDED_BY(mix_domain().token());

  // State for the internal buffer which holds intermediate mix results.
  std::unique_ptr<float[]> mix_buf_ FXL_GUARDED_BY(mix_domain().token());
  uint32_t mix_buf_frames_ FXL_GUARDED_BY(mix_domain().token()) = 0;

  // State used by the mix task.
  MixJob cur_mix_job_;
  std::optional<Format> mix_format_;

  // State used by the trim task.
  FractionalFrames<int64_t> trim_threshold_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_OUTPUT_H_
