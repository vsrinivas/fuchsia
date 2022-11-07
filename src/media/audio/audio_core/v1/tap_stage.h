// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TAP_STAGE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TAP_STAGE_H_

#include <memory>

#include "src/media/audio/audio_core/shared/mixer/output_producer.h"
#include "src/media/audio/audio_core/v1/stream.h"

namespace media::audio {

// A |TapStage| reads stream buffers from an input |ReadableStream| and copies them to a secondary
// |WritableStream|.
class TapStage : public ReadableStream {
 public:
  // Creates a |TapStage| that returns buffers from |input| while copying their contents into |tap|.
  TapStage(std::shared_ptr<ReadableStream> input, std::shared_ptr<WritableStream> tap);

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override {
    return source_->ref_time_to_frac_presentation_frame();
  }
  std::shared_ptr<Clock> reference_clock() override { return source_->reference_clock(); }
  void SetPresentationDelay(zx::duration external_delay) override;

 private:
  std::optional<ReadableStream::Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed dest_frame,
                                                     int64_t frame_count) override;
  void TrimImpl(Fixed dest_frame) override {
    // TapStage produces data on integrally-aligned frames, so Trim on integrally-aligned frames.
    source_->Trim(Fixed(dest_frame.Floor()));
  }

  void WriteSilenceToTap(int64_t frame, int64_t frame_count);
  void CopySourceToTap(const ReadableStream::Buffer& source_buffer, int64_t next_tap_frame,
                       int64_t frame_count);

  const TimelineFunction& SourceFracFrameToTapFracFrame();

  std::shared_ptr<ReadableStream> source_;
  std::shared_ptr<WritableStream> tap_;
  std::unique_ptr<OutputProducer> output_producer_;

  // Track the mapping of source frames to tap frames.
  TimelineFunction source_frac_frame_to_tap_frac_frame_;
  uint32_t source_generation_ = kInvalidGenerationId;
  uint32_t tap_generation_ = kInvalidGenerationId;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TAP_STAGE_H_
