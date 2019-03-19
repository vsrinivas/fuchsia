// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_STANDARD_OUTPUT_BASE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_STANDARD_OUTPUT_BASE_H_

#include <dispatcher-pool/dispatcher-timer.h>
#include <fuchsia/media/cpp/fidl.h>

#include "lib/media/timeline/timeline_function.h"
#include "src/lib/fxl/time/time_delta.h"
#include "src/media/audio/audio_core/audio_link.h"
#include "src/media/audio/audio_core/audio_link_packet_source.h"
#include "src/media/audio/audio_core/audio_output.h"
#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/output_producer.h"

namespace media::audio {

class AudioPacketRef;

class StandardOutputBase : public AudioOutput {
 public:
  ~StandardOutputBase() override = default;

 protected:
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
  void UpdateSourceTrans(const fbl::RefPtr<AudioRendererImpl>& audio_renderer,
                         Bookkeeping* bk);
  void UpdateDestTrans(const MixJob& job, Bookkeeping* bk);

  explicit StandardOutputBase(AudioDeviceManager* manager);

  zx_status_t Init() override;

  void Process() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  zx_status_t InitializeSourceLink(const fbl::RefPtr<AudioLink>& link) final;

  void SetNextSchedTime(fxl::TimePoint next_sched_time) {
    next_sched_time_ = next_sched_time;
    next_sched_time_known_ = true;
  }

  void SetNextSchedDelay(const fxl::TimeDelta& next_sched_delay) {
    SetNextSchedTime(fxl::TimePoint::Now() + next_sched_delay);
  }

  virtual bool StartMixJob(MixJob* job, fxl::TimePoint process_start)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) = 0;
  virtual bool FinishMixJob(const MixJob& job)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) = 0;
  void SetupMixBuffer(uint32_t max_mix_frames)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Details about the final output format
  std::unique_ptr<OutputProducer> output_producer_;

  // Timer used to schedule periodic mixing.
  fbl::RefPtr<::dispatcher::Timer> mix_timer_;

 private:
  enum class TaskType { Mix, Trim };

  void ForEachLink(TaskType task_type)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  bool SetupMix(const fbl::RefPtr<AudioRendererImpl>& audio_renderer,
                Bookkeeping* info)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool ProcessMix(const fbl::RefPtr<AudioRendererImpl>& audio_renderer,
                  Bookkeeping* info, const fbl::RefPtr<AudioPacketRef>& pkt_ref)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  bool SetupTrim(const fbl::RefPtr<AudioRendererImpl>& audio_renderer,
                 Bookkeeping* info)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool ProcessTrim(const fbl::RefPtr<AudioRendererImpl>& audio_renderer,
                   Bookkeeping* info,
                   const fbl::RefPtr<AudioPacketRef>& pkt_ref)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  fxl::TimePoint next_sched_time_;
  bool next_sched_time_known_;

  // Vector used to hold references to source links while mixing (instead of
  // holding a lock, preventing source_links_ mutation for the entire mix job).
  std::vector<fbl::RefPtr<AudioLink>> source_link_refs_
      FXL_GUARDED_BY(mix_domain_->token());

  // State for the internal buffer which holds intermediate mix results.
  std::unique_ptr<float[]> mix_buf_ FXL_GUARDED_BY(mix_domain_->token());
  uint32_t mix_buf_frames_ FXL_GUARDED_BY(mix_domain_->token()) = 0;

  // State used by the mix task.
  MixJob cur_mix_job_;

  // State used by the trim task.
  int64_t trim_threshold_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_STANDARD_OUTPUT_BASE_H_
