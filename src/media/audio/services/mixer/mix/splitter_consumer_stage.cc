// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/splitter_consumer_stage.h"

#include <lib/zx/time.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/ring_buffer_consumer_writer.h"

namespace media_audio {

SplitterConsumerStage::SplitterConsumerStage(Args args)
    : BaseConsumerStage({
          .name = args.name,
          .format = args.format,
          .reference_clock = std::move(args.reference_clock),
          .thread = std::move(args.thread),
          .writer = std::make_shared<RingBufferConsumerWriter>(std::move(args.ring_buffer)),
      }) {}

void SplitterConsumerStage::UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) {
  // First non-nullopt timeline wins.
  if (f && !presentation_time_to_frac_frame()) {
    BaseConsumerStage::UpdatePresentationTimeToFracFrame(f);
    // This store must happen *after* the above call, otherwise a thread which reads `true` from
    // this value will not see the real TimelineFunction.
    have_presentation_time_to_frac_frame_.store(true);
  }
}

void SplitterConsumerStage::FillBuffer(MixJobContext& ctx) {
  FX_CHECK(presentation_time_to_frac_frame());
  FX_CHECK(max_downstream_output_pipeline_delay_);

  const auto start_time = ctx.start_time(reference_clock());
  const auto end_time = start_time + *max_downstream_output_pipeline_delay_;

  int64_t start_frame = FrameFromPresentationTime(start_time).Floor();
  int64_t end_frame = FrameFromPresentationTime(end_time).Floor();

  // Exclude frames already written.
  start_frame = std::max(start_frame, end_of_last_fill());
  if (start_frame >= end_frame) {
    return;
  }

  CopyFromSource(ctx, start_frame, end_frame - start_frame);

  // When another thread reads this value, we must guarantee that we've written at least up until
  // this frame. Hence this store must happen *after* the above CopyFromSource.
  end_of_last_fill_.store(end_frame);
}

void SplitterConsumerStage::AdvanceSource(MixJobContext& ctx, Fixed frame) {
  if (auto s = source(); s) {
    s->Advance(ctx, frame);
  }
}

void SplitterConsumerStage::set_max_downstream_output_pipeline_delay(zx::duration delay) {
  max_downstream_output_pipeline_delay_ = delay;
}

zx::duration SplitterConsumerStage::max_downstream_output_pipeline_delay() const {
  FX_CHECK(max_downstream_output_pipeline_delay_);
  return *max_downstream_output_pipeline_delay_;
}

std::optional<TimelineFunction> SplitterConsumerStage::presentation_time_to_frac_frame() const {
  if (!have_presentation_time_to_frac_frame_.load()) {
    return std::nullopt;
  }
  auto f = PipelineStage::presentation_time_to_frac_frame();
  FX_CHECK(f);
  return f;
}

}  // namespace media_audio
