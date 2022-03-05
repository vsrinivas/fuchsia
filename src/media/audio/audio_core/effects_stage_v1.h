// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_V1_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_V1_H_

#include <memory>

#include "src/media/audio/audio_core/cached_readable_stream_buffer.h"
#include "src/media/audio/audio_core/mix_profile_config.h"
#include "src/media/audio/audio_core/pipeline_config.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/volume_curve.h"
#include "src/media/audio/lib/clock/audio_clock.h"
#include "src/media/audio/lib/effects_loader/effects_processor_v1.h"

namespace media::audio {

// An |EffectsStageV1| is a stream adapter that produces frames by reading them from a source
// |ReadableStream|, and then running a set of audio 'effects' on those frames.
class EffectsStageV1 : public ReadableStream {
 public:
  struct RingoutBuffer {
    static RingoutBuffer Create(const Format& format, const EffectsProcessorV1& processor,
                                const MixProfileConfig& mix_profile_config);
    static RingoutBuffer Create(const Format& format, uint32_t ringout_frames,
                                uint32_t max_batch_size, uint32_t block_size,
                                int64_t mix_profile_period_nsecs);

    const uint32_t total_frames = 0;
    const uint32_t buffer_frames = 0;
    std::vector<float> buffer;
  };

  static std::shared_ptr<EffectsStageV1> Create(
      const std::vector<PipelineConfig::EffectV1>& effects, std::shared_ptr<ReadableStream> source,
      const MixProfileConfig& mix_profile_config, VolumeCurve volume_curve);

  EffectsStageV1(std::shared_ptr<ReadableStream> source,
                 std::unique_ptr<EffectsProcessorV1> effects_processor,
                 const MixProfileConfig& mix_profile_config, VolumeCurve volume_curve);

  int64_t block_size() const { return effects_processor_->block_size(); }

  fpromise::result<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config);

  const EffectsProcessorV1& effects_processor() const { return *effects_processor_; }

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  AudioClock& reference_clock() override { return source_->reference_clock(); }
  std::optional<ReadableStream::Buffer> ReadLock(ReadLockContext& ctx, Fixed dest_frame,
                                                 int64_t frame_count) override;
  void Trim(Fixed dest_frame) override { source_->Trim(dest_frame); }

  void SetPresentationDelay(zx::duration external_delay) override;
  void ReportUnderflow(Fixed frac_source_start, Fixed frac_source_mix_point,
                       zx::duration underflow_duration) override {
    source_->ReportUnderflow(frac_source_start, frac_source_mix_point, underflow_duration);
  }
  void ReportPartialUnderflow(Fixed frac_source_offset, int64_t dest_mix_offset) override {
    source_->ReportPartialUnderflow(frac_source_offset, dest_mix_offset);
  }

 private:
  zx::duration ComputeIntrinsicMinLeadTime() const;

  std::shared_ptr<ReadableStream> source_;
  std::unique_ptr<EffectsProcessorV1> effects_processor_;
  VolumeCurve volume_curve_;

  // The last buffer returned from ReadLock, saved to prevent recomputing frames on
  // consecutive calls to ReadLock. This is reset once the caller has unlocked the buffer,
  // signifying that the buffer is no longer needed.
  CachedReadableStreamBuffer cached_buffer_;

  uint32_t ringout_frames_sent_ = 0;
  int64_t next_ringout_frame_ = 0;
  RingoutBuffer ringout_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_V1_H_
