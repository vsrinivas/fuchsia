// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_H_

#include <memory>

#include "src/media/audio/audio_core/pipeline_config.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/lib/effects_loader/effects_processor.h"

namespace media::audio {

// An |EffectsStage| is a stream adapter that produces frames by reading them from a source
// |ReadableStream|, and then running a set of audio 'effects' on those frames.
class EffectsStage : public ReadableStream {
 public:
  static std::shared_ptr<EffectsStage> Create(const std::vector<PipelineConfig::Effect>& effects,
                                              std::shared_ptr<ReadableStream> source);

  EffectsStage(std::shared_ptr<ReadableStream> source,
               std::unique_ptr<EffectsProcessor> effects_processor);

  uint32_t block_size() const { return effects_processor_->block_size(); }

  fit::result<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config);

  // |media::audio::ReadableStream|
  std::optional<ReadableStream::Buffer> ReadLock(zx::time ref_time, int64_t frame,
                                                 uint32_t frame_count) override;
  void Trim(zx::time trim_threshold) override { source_->Trim(trim_threshold); }
  TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const override;
  void SetMinLeadTime(zx::duration lead_time) override;
  void ReportUnderflow(FractionalFrames<int64_t> frac_source_start,
                       FractionalFrames<int64_t> frac_source_mix_point,
                       zx::duration underflow_duration) override {
    source_->ReportUnderflow(frac_source_start, frac_source_mix_point, underflow_duration);
  }
  void ReportPartialUnderflow(FractionalFrames<int64_t> frac_source_offset,
                              int64_t dest_mix_offset) override {
    source_->ReportPartialUnderflow(frac_source_offset, dest_mix_offset);
  }

 private:
  std::optional<ReadableStream::Buffer> DupCurrentBlock();
  zx::duration ComputeIntrinsicMinLeadTime() const;

  std::shared_ptr<ReadableStream> source_;
  std::unique_ptr<EffectsProcessor> effects_processor_;
  std::optional<ReadableStream::Buffer> current_block_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_H_
