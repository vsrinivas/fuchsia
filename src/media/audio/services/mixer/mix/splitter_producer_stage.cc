// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/splitter_producer_stage.h"

#include <lib/zx/time.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/mixer/mix/timeline_function_math.h"

namespace media_audio {

SplitterProducerStage::SplitterProducerStage(Args args)
    : PipelineStage(args.name, args.format, std::move(args.reference_clock)),
      ring_buffer_(std::move(args.ring_buffer)),
      consumer_(std::move(args.consumer)) {}

void SplitterProducerStage::UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) {
  set_presentation_time_to_frac_frame(f);

  // If the consumer runs on the same thread, then the consumer should start when we start.
  if (thread() == consumer_->thread()) {
    consumer_->UpdatePresentationTimeToFracFrame(f);
  }

  // This offset may have changed.
  RecomputeConsumerFrameOffset();
}

void SplitterProducerStage::AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) {
  if (thread() != consumer_->thread()) {
    return;
  }

  FX_CHECK(consumer_frame_offset_);
  ScopedThreadChecker checker(consumer_->thread()->checker());
  consumer_->AdvanceSource(ctx, frame + *consumer_frame_offset_);
}

std::optional<PipelineStage::Packet> SplitterProducerStage::ReadImpl(MixJobContext& ctx,
                                                                     Fixed start_frame,
                                                                     int64_t frame_count) {
  // Check if the consumer is stopped. The consumer is started when the first same-thread producer
  // starts. Our thread should be started (otherwise Read should not be called). Hence, the consumer
  // can be stopped only if the consumer runs on a different thread.
  if (!consumer_->presentation_time_to_frac_frame()) {
    FX_CHECK(thread() != consumer_->thread());
    return std::nullopt;
  }

  if (thread() == consumer_->thread()) {
    // This must have been computed by a prior call to UpdatePresentationTimeToFracFrame.
    FX_CHECK(consumer_frame_offset_);
    // Ensure the buffer is up-to-date for our current mix job.
    // When the consumer runs on the same thread, it's our responsibility to drive the consumer.
    ScopedThreadChecker checker(consumer_->thread()->checker());
    consumer_->FillBuffer(ctx);
  } else {
    // If the consumer runs on a different thread, this may not have been computed yet.
    if (!consumer_frame_offset_) {
      RecomputeConsumerFrameOffset();
      FX_CHECK(consumer_frame_offset_);
    }
  }

  // Intersect our request with what the consumer has written so far. If some frames are missing,
  // the consumer has underflowed.
  const int64_t consumer_start_frame = Fixed(start_frame + *consumer_frame_offset_).Floor();
  /***/ int64_t consumer_end_frame = consumer_start_frame + frame_count;
  const int64_t consumer_end_of_available_frames = consumer_->end_of_last_fill();
  if (consumer_end_frame > consumer_end_of_available_frames) {
    if (consumer_start_frame >= consumer_end_of_available_frames) {
      // TODO(fxbug.dev/87651): log an underflow of `consumer_end_frame -
      // consumer_end_of_available_frames` frames. Should this FX_CHECK if we're on the same thread
      // as the consumer?
      return std::nullopt;
    }
    consumer_end_frame = consumer_end_of_available_frames;
  }

  // We don't need to cache the returned packet since we don't generate any data dynamically.
  auto packet = ring_buffer_->Read(consumer_start_frame, consumer_end_frame - consumer_start_frame);
  return MakeUncachedPacket(packet.start() - *consumer_frame_offset_, packet.length(),
                            packet.payload());
}

void SplitterProducerStage::RecomputeConsumerFrameOffset() {
  const auto consumer_presentation_time_to_frac_frame =
      consumer_->presentation_time_to_frac_frame();

  if (!consumer_presentation_time_to_frac_frame || !presentation_time_to_frac_frame()) {
    consumer_frame_offset_ = std::nullopt;
    return;
  }

  // Solve for `consumer` - `producer`.
  consumer_frame_offset_ = TimelineFunctionOffsetInFracFrames(
      *presentation_time_to_frac_frame(), *consumer_presentation_time_to_frac_frame);
}

}  // namespace media_audio
