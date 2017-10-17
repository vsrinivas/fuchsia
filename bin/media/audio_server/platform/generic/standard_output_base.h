// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/audio/dispatcher-pool/dispatcher-timer.h"
#include "garnet/bin/media/audio_server/audio_output.h"
#include "garnet/bin/media/audio_server/audio_renderer_to_output_link.h"
#include "garnet/bin/media/audio_server/gain.h"
#include "garnet/bin/media/audio_server/platform/generic/mixer.h"
#include "garnet/bin/media/audio_server/platform/generic/output_formatter.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/media/fidl/media_result.fidl.h"
#include "lib/media/fidl/media_types.fidl.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {
namespace audio {

class StandardOutputBase : public AudioOutput {
 public:
  ~StandardOutputBase() override;

 protected:
  struct MixJob {
    static constexpr uint32_t kInvalidGeneration = 0;

    // State for the job set up once by the output implementation and then used
    // by all renderers.
    void* buf;
    uint32_t buf_frames;
    int64_t start_pts_of;  // start PTS, expressed in output frames.
    uint32_t local_to_output_gen;
    bool accumulate;
    const TimelineFunction* local_to_output;

    // State for the job which is set up for each renderer during SetupMix
    uint32_t frames_produced;
  };

  struct RendererBookkeeping : public AudioRendererToOutputLink::Bookkeeping {
    RendererBookkeeping();
    ~RendererBookkeeping() override;

    // The output values of these functions are in fractional frames.
    TimelineFunction local_time_to_renderer_subframes;
    TimelineFunction output_frames_to_renderer_subframes;

    TimelineFunction local_time_to_renderer_frames;
    TimelineFunction output_frames_to_renderer_frames;

    uint32_t local_time_to_renderer_subframes_gen = 0;
    uint32_t out_frames_to_renderer_subframes_gen = MixJob::kInvalidGeneration;
    uint32_t step_size;
    Gain::AScale amplitude_scale;
    MixerPtr mixer;

    void UpdateRendererTrans(const AudioRendererImplPtr& renderer,
                             const AudioRendererFormatInfo& format_info);
    void UpdateOutputTrans(const MixJob& job);
  };

  explicit StandardOutputBase(AudioOutputManager* manager);

  MediaResult Init() override;

  void Process() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  MediaResult InitializeLink(const AudioRendererToOutputLinkPtr& link) final;

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
  virtual RendererBookkeeping* AllocBookkeeping();
  void SetupMixBuffer(uint32_t max_mix_frames)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Details about the final output format
  OutputFormatterPtr output_formatter_;

  // Timer used to schedule periodic mixing.
  fbl::RefPtr<::audio::dispatcher::Timer> mix_timer_;

 private:
  enum class TaskType { Mix, Trim };

  void ForeachRenderer(TaskType task_type)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  bool SetupMix(const AudioRendererImplPtr& renderer, RendererBookkeeping* info)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool ProcessMix(const AudioRendererImplPtr& renderer,
                  RendererBookkeeping* info,
                  const AudioPipe::AudioPacketRefPtr& pkt_ref)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  bool SetupTrim(const AudioRendererImplPtr& renderer,
                 RendererBookkeeping* info)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool ProcessTrim(const AudioRendererImplPtr& renderer,
                   RendererBookkeeping* info,
                   const AudioPipe::AudioPacketRefPtr& pkt_ref)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  fxl::TimePoint next_sched_time_;
  bool next_sched_time_known_;

  // Vector used to hold references to our renderer links while we are mixing
  // (instead of holding the lock which prevents links_ mutation for the entire
  // mix job)
  std::vector<std::shared_ptr<AudioRendererToOutputLink>> link_refs_
      FXL_GUARDED_BY(mix_domain_->token());

  // State for the internal buffer which holds intermediate mix results.
  //
  // TODO(johngro): Right now, the cannonical intermediate format is signed 32
  // bit ints.  As time goes on, we may need to reconsider this.  This will
  // become more important when...
  //
  // 1) We support 24 bit audio.  Right now, with a 16 bit max, we can
  //    accumulate for up to a maximum of 2^16-1 renderers without needing to do
  //    anything special about about clipping.  With 24 bit audio, this number
  //    will drop to only 255 simultanious renderers.  It is unclear if this is
  //    a reasonable system-wide limitation or not.
  // 2) We support floating point audio.
  std::unique_ptr<int32_t[]> mix_buf_ FXL_GUARDED_BY(mix_domain_->token());
  uint32_t mix_buf_frames_ FXL_GUARDED_BY(mix_domain_->token()) = 0;

  // State used by the mix task.
  MixJob cur_mix_job_;

  // State used by the trim task.
  int64_t trim_threshold_;
};

}  // namespace audio
}  // namespace media
