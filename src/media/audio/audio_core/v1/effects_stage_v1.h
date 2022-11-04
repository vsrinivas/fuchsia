// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_EFFECTS_STAGE_V1_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_EFFECTS_STAGE_V1_H_

#include <memory>

#include <gtest/gtest_prod.h>

#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/pipeline_config.h"
#include "src/media/audio/audio_core/v1/reusable_buffer.h"
#include "src/media/audio/audio_core/v1/stream.h"
#include "src/media/audio/audio_core/v1/volume_curve.h"
#include "src/media/audio/lib/effects_loader/effects_processor_v1.h"

namespace media::audio {

// An |EffectsStageV1| is a stream adapter that produces frames by reading them from a source
// |ReadableStream|, and then running a set of audio 'effects' on those frames.
class EffectsStageV1 : public ReadableStream {
 public:
  static std::shared_ptr<EffectsStageV1> Create(
      const std::vector<PipelineConfig::EffectV1>& effects, std::shared_ptr<ReadableStream> source,
      VolumeCurve volume_curve);

  EffectsStageV1(std::shared_ptr<ReadableStream> source,
                 std::unique_ptr<EffectsProcessorV1> effects_processor, VolumeCurve volume_curve);

  int64_t block_size() const { return effects_processor_->block_size(); }

  fpromise::result<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config);

  const EffectsProcessorV1& effects_processor() const { return *effects_processor_; }

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  std::shared_ptr<Clock> reference_clock() override { return source_->reference_clock(); }

  void SetPresentationDelay(zx::duration external_delay) override;

 private:
  FRIEND_TEST(EffectsStageFrameBufferTest, Append);
  FRIEND_TEST(EffectsStageFrameBufferTest, AppendSilence);

  std::optional<ReadableStream::Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed dest_frame,
                                                     int64_t frame_count) override;
  void TrimImpl(Fixed dest_frame) override;
  int64_t FillCache(ReadLockContext& ctx, Fixed dest_frame, int64_t frame_count);
  zx::duration ComputeIntrinsicMinLeadTime() const;

  std::shared_ptr<ReadableStream> source_;
  std::unique_ptr<EffectsProcessorV1> effects_processor_;
  VolumeCurve volume_curve_;

  // Each batch must have a multiple of block_size_frames_ and a maximum of max_batch_size_frames_.
  const int64_t block_size_frames_;
  const int64_t max_batch_size_frames_;

  // We must process frames in batches that are multiples of effects_processor_->batch_size().
  // Our cache accumulates data from source_ until we've buffered at least one full batch,
  // at which point we run the effect and store the output of the effect in cache_.dest_buffer.
  // The cache lives until we Trim past source_buffer_.end().
  //
  // For example:
  //
  //   +------------------------+
  //   |     source_buffer_     |
  //   +------------------------+
  //   ^       ^        ^       ^      ^
  //   A       B        C       D      E
  //
  // 1. Caller asks for frames [A,B). Assume D = A+block_size. We read frames [A,D) from
  //    source_ into source_buffer_, then process those frames, leaving the processed
  //    data in cache_.dest_buffer. We return processed frames [A,B).
  //
  // 2. Caller asks for frames [B,C). This intersects source_buffer_, so we return
  //    processed frames [B,C).
  //
  // 3. Caller asks for frames [C,E). This intersects source_buffer_, so we return processed
  //    frames [C,D). When the caller is done with those frames, we receive a Trim(D) call
  //    (via ReadableStream::Buffer::~Buffer), which sets cache_ to std::nullopt.
  //
  // 4. Caller asks for frames [D,E). The above process repeats.
  struct Cache {
    // Properties of the cached source buffer.
    StreamUsageMask source_usage_mask;
    float source_total_applied_gain_db;
    // Destination frames after processing. This refers to the same set of frames as source_buffer_,
    // and if the effect processes in-place, it points at source_buffer_.payload().
    float* dest_buffer;
  };
  std::optional<Cache> cache_;

  // This is non-empty iff cache_ != std::nullopt.
  ReusableBuffer source_buffer_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_EFFECTS_STAGE_V1_H_
