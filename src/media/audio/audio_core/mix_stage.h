// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIX_STAGE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIX_STAGE_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/zx/time.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/versioned_timeline_function.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio {

class MixStage : public ReadableStream {
 public:
  MixStage(const Format& output_format, uint32_t block_size,
           TimelineFunction reference_clock_to_fractional_frame, AudioClock ref_clock);
  MixStage(const Format& output_format, uint32_t block_size,
           fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame,
           AudioClock ref_clock);

  // |media::audio::ReadableStream|
  std::optional<ReadableStream::Buffer> ReadLock(zx::time dest_ref_time, int64_t frame,
                                                 uint32_t frame_count) override;
  void Trim(zx::time dest_ref_time) override;
  TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const override;
  AudioClock reference_clock() const override { return output_ref_clock_; }
  void SetMinLeadTime(zx::duration min_lead_time) override;

  std::shared_ptr<Mixer> AddInput(std::shared_ptr<ReadableStream> stream,
                                  Mixer::Resampler sampler_hint = Mixer::Resampler::Default);
  void RemoveInput(const ReadableStream& stream);

 private:
  MixStage(std::shared_ptr<WritableStream> output_stream);
  void SetupMixBuffer(uint32_t max_mix_frames);

  struct MixJob {
    // Job state set up once by an output implementation, used by all renderers.
    float* buf;
    uint32_t buf_frames;
    int64_t start_pts_of;  // start PTS, expressed in output frames.
    TimelineFunction dest_ref_clock_to_frac_dest_frame;
    bool accumulate;
    StreamUsageMask usages_mixed;
    float applied_gain_db;

    // Per-stream job state, set up for each renderer during SetupMix.
    uint32_t frames_produced;
  };

  // TODO(13415): Integrate it into the Mixer class itself.
  void UpdateSourceTrans(const ReadableStream& stream, Mixer::Bookkeeping* bk);
  void UpdateDestTrans(const MixJob& job, Mixer::Bookkeeping* bk);

  enum class TaskType { Mix, Trim };

  void ForEachSource(TaskType task_type, zx::time dest_ref_time);

  void SetupMix(Mixer* mixer);
  bool ProcessMix(ReadableStream* stream, Mixer* mixer, const ReadableStream::Buffer& buffer);
  void MixStream(ReadableStream* stream, Mixer* mixer, zx::time dest_ref_time);

  struct StreamHolder {
    std::shared_ptr<ReadableStream> stream;
    std::shared_ptr<Mixer> mixer;
  };

  std::mutex stream_lock_;
  std::vector<StreamHolder> streams_ FXL_GUARDED_BY(stream_lock_);

  std::shared_ptr<WritableStream> output_stream_;

  // State used by the mix task.
  MixJob cur_mix_job_;

  // We are passed our destination stream's reference clock at construction time, and we cache it
  // here, so we need not do multiple levels of dereferencing at clock-read time.
  AudioClock output_ref_clock_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIX_STAGE_H_
