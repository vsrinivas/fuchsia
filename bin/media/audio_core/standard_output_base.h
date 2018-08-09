// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_STANDARD_OUTPUT_BASE_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_STANDARD_OUTPUT_BASE_H_

#include <dispatcher-pool/dispatcher-timer.h>
#include <fuchsia/media/cpp/fidl.h>

#include "garnet/bin/media/audio_core/audio_link.h"
#include "garnet/bin/media/audio_core/audio_link_packet_source.h"
#include "garnet/bin/media/audio_core/audio_output.h"
#include "garnet/bin/media/audio_core/constants.h"
#include "garnet/bin/media/audio_core/gain.h"
#include "garnet/bin/media/audio_core/mixer/mixer.h"
#include "garnet/bin/media/audio_core/mixer/output_producer.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {
namespace audio {

class AudioPacketRef;

class StandardOutputBase : public AudioOutput {
 public:
  ~StandardOutputBase() override;

 protected:
  struct MixJob {
    // State for the job set up once by the output implementation and then used
    // by all audio outs.
    void* buf;
    uint32_t buf_frames;
    int64_t start_pts_of;  // start PTS, expressed in output frames.
    uint32_t local_to_output_gen;
    bool accumulate;
    const TimelineFunction* local_to_output;

    float sw_output_db_gain;
    bool sw_output_muted;

    // State for the job which is set up for each audio out during SetupMix
    uint32_t frames_produced;
  };

  // TODO(mpuryear): per MTWN-129, combine this with CaptureLinkBookkeeping, and
  // integrate it into the Mixer class itself.
  // TODO(mpuryear): Rationalize naming and usage of the bookkeeping structs.
  struct AudioOutBookkeeping : public AudioLink::Bookkeeping {
    AudioOutBookkeeping();
    ~AudioOutBookkeeping() override;

    // The output values of these functions are in fractional frames.
    TimelineFunction local_time_to_audio_out_subframes;
    TimelineFunction output_frames_to_audio_out_subframes;

    TimelineFunction local_time_to_audio_out_frames;
    TimelineFunction output_frames_to_audio_out_frames;

    uint32_t local_time_to_audio_out_subframes_gen = kInvalidGenerationId;
    uint32_t out_frames_to_audio_out_subframes_gen = kInvalidGenerationId;
    uint32_t step_size;
    uint32_t modulo;
    uint32_t denominator() const {
      return output_frames_to_audio_out_subframes.rate().reference_delta();
    }
    Gain::AScale amplitude_scale;
    MixerPtr mixer;

    void UpdateAudioOutTrans(const fbl::RefPtr<AudioOutImpl>& audio_out,
                             const AudioOutFormatInfo& format_info);
    void UpdateOutputTrans(const MixJob& job);
  };

  explicit StandardOutputBase(AudioDeviceManager* manager);

  zx_status_t Init() override;

  void Process() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  zx_status_t InitializeSourceLink(const AudioLinkPtr& link) final;

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
  virtual AudioOutBookkeeping* AllocBookkeeping();
  void SetupMixBuffer(uint32_t max_mix_frames)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Details about the final output format
  OutputProducerPtr output_producer_;

  // Timer used to schedule periodic mixing.
  fbl::RefPtr<::dispatcher::Timer> mix_timer_;

 private:
  enum class TaskType { Mix, Trim };

  void ForeachLink(TaskType task_type)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  bool SetupMix(const fbl::RefPtr<AudioOutImpl>& audio_out,
                AudioOutBookkeeping* info)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool ProcessMix(const fbl::RefPtr<AudioOutImpl>& audio_out,
                  AudioOutBookkeeping* info,
                  const fbl::RefPtr<AudioPacketRef>& pkt_ref)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  bool SetupTrim(const fbl::RefPtr<AudioOutImpl>& audio_out,
                 AudioOutBookkeeping* info)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool ProcessTrim(const fbl::RefPtr<AudioOutImpl>& audio_out,
                   AudioOutBookkeeping* info,
                   const fbl::RefPtr<AudioPacketRef>& pkt_ref)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  fxl::TimePoint next_sched_time_;
  bool next_sched_time_known_;

  // Vector used to hold references to our source links while we are mixing
  // (instead of holding the lock which prevents source_links_ mutation for the
  // entire mix job)
  std::vector<std::shared_ptr<AudioLink>> source_link_refs_
      FXL_GUARDED_BY(mix_domain_->token());

  // State for the internal buffer which holds intermediate mix results.
  std::unique_ptr<float[]> mix_buf_ FXL_GUARDED_BY(mix_domain_->token());
  uint32_t mix_buf_frames_ FXL_GUARDED_BY(mix_domain_->token()) = 0;

  // State used by the mix task.
  MixJob cur_mix_job_;

  // State used by the trim task.
  int64_t trim_threshold_;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_STANDARD_OUTPUT_BASE_H_
