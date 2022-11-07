// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIX_STAGE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIX_STAGE_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/zx/time.h>

#include <optional>

#include <gtest/gtest_prod.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/shared/mixer/mixer.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/stream.h"
#include "src/media/audio/audio_core/v1/versioned_timeline_function.h"
#include "src/media/audio/lib/clock/clock_synchronizer.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

class MixStage : public ReadableStream {
 public:
  MixStage(const Format& output_format, uint32_t block_size,
           TimelineFunction ref_time_to_frac_presentation_frame, std::shared_ptr<Clock> ref_clock,
           std::optional<float> min_gain_db = std::nullopt,
           std::optional<float> max_gain_db = std::nullopt);
  MixStage(const Format& output_format, uint32_t block_size,
           fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
           std::shared_ptr<Clock> ref_clock, std::optional<float> min_gain_db = std::nullopt,
           std::optional<float> max_gain_db = std::nullopt);

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  std::shared_ptr<Clock> reference_clock() override { return output_ref_clock_; }
  void SetPresentationDelay(zx::duration external_delay) override;

  std::shared_ptr<Mixer> AddInput(std::shared_ptr<ReadableStream> stream,
                                  std::optional<float> initial_dest_gain_db = std::nullopt,
                                  Mixer::Resampler sampler_hint = Mixer::Resampler::Default);
  void RemoveInput(const ReadableStream& stream);

 private:
  FRIEND_TEST(MixStageTest, DontCrashOnDestOffsetRoundingError);

  std::optional<ReadableStream::Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed dest_frame,
                                                     int64_t frame_count) override;
  void TrimImpl(Fixed dest_frame) override;
  void SetupMixBuffer(uint32_t max_mix_frames);

  struct MixJob {
    // Job state set up once by an output implementation, used by all renderers.
    // TODO(fxbug.dev/13415): Integrate it into the Mixer class itself.
    ReadLockContext* read_lock_ctx;
    float* buf;
    int64_t buf_frames;
    int64_t dest_start_frame;
    TimelineFunction dest_ref_clock_to_frac_dest_frame;
    bool accumulate;
    StreamUsageMask usages_mixed;
    float total_applied_gain_db;
  };

  struct StreamHolder {
    std::shared_ptr<ReadableStream> stream;
    std::shared_ptr<ReadableStream> original_stream;
    std::shared_ptr<Mixer> mixer;
    std::shared_ptr<::media_audio::ClockSynchronizer> clock_sync;
  };

  enum class TaskType { Mix, Trim };
  void ForEachSource(TaskType task_type, Fixed dest_frame);
  void ReconcileClocksAndSetStepSize(::media_audio::ClockSynchronizer& clock_sync, Mixer& mixer,
                                     ReadableStream& stream);
  void SyncSourcePositionFromClocks(::media_audio::ClockSynchronizer& clock_sync,
                                    const Clock& source_clock, const Clock& dest_clock,
                                    Mixer& mixer, int64_t dest_frame, zx::time mono_now_from_dest,
                                    bool timeline_changed);

  void MixStream(Mixer& mixer, ReadableStream& stream);
  std::optional<Buffer> NextSourceBuffer(Mixer& mixer, ReadableStream& stream,
                                         int64_t dest_frames) const;

  std::mutex stream_lock_;
  std::vector<StreamHolder> streams_ FXL_GUARDED_BY(stream_lock_);

  // State used by the mix task.
  MixJob cur_mix_job_;

  const int64_t output_buffer_frames_;
  std::vector<float> output_buffer_;
  std::shared_ptr<Clock> output_ref_clock_;
  fbl::RefPtr<VersionedTimelineFunction> output_ref_clock_to_fractional_frame_;

  const Gain::Limits gain_limits_;

  // Used to variably throttle the amount of jam-sync-related logging we produce
  uint32_t jam_sync_count_ = 0;

  // This is used by ForEachSource to snapshot `streams_`. It should be safe to store here because
  // ForEachSource is always called by the same thread. This avoids an allocation in ForEachSource.
  std::vector<StreamHolder> for_each_source_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIX_STAGE_H_
