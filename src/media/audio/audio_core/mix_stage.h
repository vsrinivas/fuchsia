// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIX_STAGE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIX_STAGE_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/zx/time.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/versioned_timeline_function.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

class MixStage : public ReadableStream {
 public:
  MixStage(const Format& output_format, uint32_t block_size,
           TimelineFunction ref_time_to_frac_presentation_frame, AudioClock& ref_clock);
  MixStage(const Format& output_format, uint32_t block_size,
           fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
           AudioClock& ref_clock);

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  AudioClock& reference_clock() override { return output_ref_clock_; }
  std::optional<ReadableStream::Buffer> ReadLock(int64_t dest_frame, size_t frame_count) override;
  void Trim(int64_t dest_frame) override;
  void SetMinLeadTime(zx::duration min_lead_time) override;

  std::shared_ptr<Mixer> AddInput(std::shared_ptr<ReadableStream> stream,
                                  std::optional<float> initial_dest_gain_db = std::nullopt,
                                  Mixer::Resampler sampler_hint = Mixer::Resampler::Default);
  void RemoveInput(const ReadableStream& stream);

 private:
  void SetupMixBuffer(uint32_t max_mix_frames);

  struct MixJob {
    // Job state set up once by an output implementation, used by all renderers.
    // TODO(13415): Integrate it into the Mixer class itself.
    float* buf;
    uint32_t buf_frames;
    int64_t dest_start_frame;
    TimelineFunction dest_ref_clock_to_frac_dest_frame;
    bool accumulate;
    StreamUsageMask usages_mixed;
    float applied_gain_db;
  };

  struct StreamHolder {
    std::shared_ptr<ReadableStream> stream;
    std::shared_ptr<Mixer> mixer;
  };

  enum class TaskType { Mix, Trim };
  void ForEachSource(TaskType task_type, int64_t dest_frame);
  void ReconcileClocksAndSetStepSize(Mixer::SourceInfo& info, Mixer::Bookkeeping& bookkeeping,
                                     ReadableStream& stream);

  void MixStream(Mixer& mixer, ReadableStream& stream);
  bool ProcessMix(Mixer& mixer, ReadableStream& stream, const ReadableStream::Buffer& buffer);

  std::mutex stream_lock_;
  std::vector<StreamHolder> streams_ FXL_GUARDED_BY(stream_lock_);

  // State used by the mix task.
  MixJob cur_mix_job_;

  const size_t output_buffer_frames_;
  std::vector<float> output_buffer_;
  AudioClock& output_ref_clock_;
  fbl::RefPtr<VersionedTimelineFunction> output_ref_clock_to_fractional_frame_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIX_STAGE_H_
