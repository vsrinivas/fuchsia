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
#include "src/media/audio/audio_core/mixer/output_producer.h"

namespace media::audio {

class RingBuffer;

class AudioPacketRef;

class AudioOutput : public AudioDevice {
 public:
  ~AudioOutput() override = default;

  // Minimum clock lead time for this output
  zx::duration min_lead_time() const override { return min_lead_time_; }

 protected:
  AudioOutput(ThreadingModel* threading_model, DeviceRegistry* registry);

  struct MixJob {
    // Job state set up once by an output implementation, used by all renderers.
    void* buf;
    uint32_t buf_frames;
    int64_t start_pts_of;  // start PTS, expressed in output frames.
    uint32_t local_to_output_gen;
    bool accumulate;
    const TimelineFunction* local_to_output;

    float sw_output_gain_db;
    bool sw_output_muted;

    // Per-stream job state, set up for each renderer during SetupMix.
    uint32_t frames_produced;
  };

  // TODO(mpuryear): per MTWN-129, integrate it into the Mixer class itself.
  // TODO(mpuryear): Rationalize naming/usage of bookkeeping and MixJob structs.
  void UpdateSourceTrans(const fbl::RefPtr<AudioObject>& source, Mixer::Bookkeeping* bk);
  void UpdateDestTrans(const MixJob& job, Mixer::Bookkeeping* bk);

  void Cleanup() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void Process() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  zx_status_t InitializeSourceLink(const fbl::RefPtr<AudioLink>& link) final;

  void SetNextSchedTime(zx::time next_sched_time) {
    next_sched_time_ = next_sched_time;
    next_sched_time_known_ = true;
  }

  void SetNextSchedDelay(const zx::duration& next_sched_delay) {
    auto now = async::Now(mix_domain().dispatcher());
    SetNextSchedTime(now + next_sched_delay);
  }

  virtual bool StartMixJob(MixJob* job, zx::time process_start)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;
  virtual bool FinishMixJob(const MixJob& job)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) = 0;
  void SetupMixBuffer(uint32_t max_mix_frames) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // Details about the final output format
  std::unique_ptr<OutputProducer> output_producer_;

  // Timer used to schedule periodic mixing.
  void MixTimerThunk() {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &mix_domain());
    Process();
  }
  async::TaskClosureMethod<AudioOutput, &AudioOutput::MixTimerThunk> mix_timer_
      FXL_GUARDED_BY(mix_domain().token()){this};

  zx::duration min_lead_time_;

 private:
  enum class TaskType { Mix, Trim };

  void ForEachLink(TaskType task_type) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void SetupMix(Mixer* mixer) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());
  bool ProcessMix(const fbl::RefPtr<AudioObject>& source, Mixer* mixer,
                  const fbl::RefPtr<AudioPacketRef>& pkt_ref)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void SetupTrim(Mixer* mixer) FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());
  bool ProcessTrim(const fbl::RefPtr<AudioPacketRef>& pkt_ref)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  zx::time next_sched_time_;
  bool next_sched_time_known_;

  // Vector used to hold references to source links while mixing (instead of
  // holding a lock, preventing source_links_ mutation for the entire mix job).
  std::vector<fbl::RefPtr<AudioLink>> source_link_refs_ FXL_GUARDED_BY(mix_domain().token());

  // State for the internal buffer which holds intermediate mix results.
  std::unique_ptr<float[]> mix_buf_ FXL_GUARDED_BY(mix_domain().token());
  uint32_t mix_buf_frames_ FXL_GUARDED_BY(mix_domain().token()) = 0;

  // State used by the mix task.
  MixJob cur_mix_job_;

  // State used by the trim task.
  int64_t trim_threshold_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_OUTPUT_H_
