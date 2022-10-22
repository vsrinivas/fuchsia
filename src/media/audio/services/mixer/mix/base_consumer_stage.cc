// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/base_consumer_stage.h"

#include <lib/zx/time.h>

namespace media_audio {

BaseConsumerStage::BaseConsumerStage(Args args)
    : PipelineStage(args.name, args.format, args.reference_clock),
      writer_(std::move(args.writer)) {}

void BaseConsumerStage::AddSource(PipelineStagePtr source, AddSourceOptions options) {
  FX_CHECK(source);
  FX_CHECK(!source_) << "Consumer already connected to source " << source_->name();
  source_ = std::move(source);
  source_->UpdatePresentationTimeToFracFrame(presentation_time_to_frac_frame());
}

void BaseConsumerStage::RemoveSource(PipelineStagePtr source) {
  FX_CHECK(source);
  FX_CHECK(source_ == source) << "Consumer not connected to source " << source->name();
  // When the source is disconnected, it's effectively "stopped". Updating the timeline function to
  // "stopped" will help catch bugs where a source is accidentally Read or Advance'd while detached.
  source_->UpdatePresentationTimeToFracFrame(std::nullopt);
  source_ = nullptr;
}

void BaseConsumerStage::UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) {
  set_presentation_time_to_frac_frame(f);
  if (source_) {
    source_->UpdatePresentationTimeToFracFrame(f);
  }
}

void BaseConsumerStage::CopyFromSource(MixJobContext& ctx, int64_t start_frame, int64_t length) {
  const int64_t end_frame = start_frame + length;

  while (start_frame < end_frame) {
    int64_t length = end_frame - start_frame;
    std::optional<Packet> packet;
    if (source_) {
      packet = source_->Read(ctx, Fixed(start_frame), length);
    }
    if (!packet) {
      writer_->WriteSilence(start_frame, length);
      return;
    }

    // SampleAndHold: frame 1.X overlaps frame 2.0, so always round up.
    auto packet_start_frame = packet->start().Ceiling();
    if (packet_start_frame > start_frame) {
      writer_->WriteSilence(start_frame, packet_start_frame - start_frame);
    }

    writer_->WriteData(packet_start_frame, packet->length(), packet->payload());
    start_frame = packet->end().Ceiling();
  }
}

}  // namespace media_audio
