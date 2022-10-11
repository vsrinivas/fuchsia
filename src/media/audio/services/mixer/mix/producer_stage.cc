// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/producer_stage.h"

#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <utility>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"

namespace media_audio {

ProducerStage::ProducerStage(Args args)
    : PipelineStage(args.name, args.format, args.reference_clock),
      internal_source_(std::move(args.internal_source)),
      pending_start_stop_command_(std::move(args.pending_start_stop_command)),
      start_stop_control_(args.format, args.reference_clock) {
  FX_CHECK(internal_source_);
  FX_CHECK(internal_source_->format() == format());
  FX_CHECK(internal_source_->reference_clock() == reference_clock());
  FX_CHECK(pending_start_stop_command_);
}

void ProducerStage::AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) {
  PopPendingCommand();
  AdvanceStartStopControlTo(ctx, *DownstreamFrameToPresentationTime(frame));

  // Advance the internal frame timeline if it is started.
  if (internal_frame_offset_) {
    internal_source_->Advance(ctx, frame + *internal_frame_offset_);
  }
}

std::optional<PipelineStage::Packet> ProducerStage::ReadImpl(MixJobContext& ctx, Fixed start_frame,
                                                             int64_t frame_count) {
  PopPendingCommand();
  AdvanceStartStopControlTo(ctx, *DownstreamFrameToPresentationTime(start_frame));

  Fixed end_frame = start_frame + Fixed(frame_count);

  // Shrink the request to ignore instants when this producer's internal frame timeline is stopped.
  // The first step of PipelineStage::Read is to `AdvanceSelf(start_frame)`. Hence, we've already
  // applied all pending Start and Stop command up through `start_frame`.
  if (presentation_time_to_internal_frac_frame_) {
    // The producer is currently started. Shrink the request if the producer stops before
    // `end_frame`.
    if (auto cmd = NextCommand(ctx); cmd && !cmd->is_start && cmd->downstream_frame < end_frame) {
      end_frame = cmd->downstream_frame;
    }
  } else {
    // The producer is currently stopped. If the producer starts before `end_frame`, advance to that
    // starting frame.
    if (auto cmd = NextCommand(ctx); cmd && cmd->is_start && cmd->downstream_frame < end_frame) {
      start_frame = cmd->downstream_frame;
      AdvanceStartStopControlTo(ctx, cmd->presentation_time);
      FX_CHECK(start_stop_control_.is_started());
      // The client might want to stop before `end_frame`, but with our current APIs, this is
      // impossible: there can be at most one pending StartCommand or StopCommand, and since we just
      // applied a pending StartCommand, there cannot be a pending StopCommand queued after that.
    } else {
      // The producer is stopped for the entire request.
      return std::nullopt;
    }
  }

  // Update `frame_count` to match the region where the producer is started.
  frame_count = Fixed(end_frame - start_frame).Ceiling();

  // Translate from downstream to internal frames.
  start_frame += *internal_frame_offset_;

  // The resulting packet uses internal frames. It must be translated back to downstream frames.
  auto packet = internal_source_->Read(ctx, start_frame, frame_count);
  if (!packet) {
    return std::nullopt;
  }
  return ForwardPacket(std::move(packet), packet->start() - *internal_frame_offset_);
}

std::optional<ProducerStage::CommandSummary> ProducerStage::NextCommand(const MixJobContext& ctx) {
  // Cannot be called while the downstream timeline is stopped.
  FX_CHECK(presentation_time_to_frac_frame());

  auto pending = start_stop_control_.PendingCommand(ctx.clocks());
  if (!pending) {
    return std::nullopt;
  }

  return CommandSummary{
      .is_start = pending->second,
      .presentation_time = pending->first.reference_time,
      .internal_frame = pending->first.frame,
      .downstream_frame = *PresentationTimeToDownstreamFrame(pending->first.reference_time),
  };
}

void ProducerStage::PopPendingCommand() {
  auto cmd_or_null = pending_start_stop_command_->pop();
  if (!cmd_or_null) {
    return;
  }

  if (std::holds_alternative<StartCommand>(*cmd_or_null)) {
    start_stop_control_.Start(std::move(std::get<StartCommand>(*cmd_or_null)));
  } else {
    start_stop_control_.Stop(std::move(std::get<StopCommand>(*cmd_or_null)));
  }
}

void ProducerStage::AdvanceStartStopControlTo(const MixJobContext& ctx,
                                              zx::time presentation_time) {
  start_stop_control_.AdvanceTo(ctx.clocks(), presentation_time);

  // Recompute the translation between internal and downstream timeline, if needed.
  // Additional updates when the timeline changes.
  if (auto f = start_stop_control_.presentation_time_to_frac_frame();
      f != presentation_time_to_internal_frac_frame_) {
    presentation_time_to_internal_frac_frame_ = f;
    internal_source_->UpdatePresentationTimeToFracFrame(f);
    RecomputeInternalFrameOffset();
  }
}

void ProducerStage::UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) {
  set_presentation_time_to_frac_frame(f);
  RecomputeInternalFrameOffset();
}

void ProducerStage::RecomputeInternalFrameOffset() {
  // If downstream or internal time is stopped, the offset is not computable.
  if (!presentation_time_to_frac_frame() || !presentation_time_to_internal_frac_frame_) {
    internal_frame_offset_ = std::nullopt;
    return;
  }

  // Translations from presentation time to frame are defined by linear functions of the form:
  //
  // ```
  // f(t) = (t-t0) * fps + f0
  // ```
  //
  // This function is defined for both downstream and internal frames. Since both frame timelines
  // use the same frame rate, their time-to-frame translation functions have the same slope, meaning
  // they are offset by a constant amount. Hence, to translate from a downstream frame to an
  // internal frame, we need an `offset` such that:
  //
  // ```
  // f_internal(t) = f_downstream(t) + offset
  // ```
  //
  // Solving for `offset`, we have:
  //
  // ```
  // offset = f_internal(t) - f_downstream(t)
  //        = (t-t0_internal) * fps + f0_internal - (t-t0_downstream)*fps - f0_downstream
  //        = (t0_downstream - t0_internal) * fps + f0_internal - f0_downstream
  // ```
  //
  // This is computed below.
  int64_t t0_internal = presentation_time_to_internal_frac_frame_->reference_time();
  int64_t t0_downstream = presentation_time_to_frac_frame()->reference_time();
  auto f0_internal = Fixed::FromRaw(presentation_time_to_internal_frac_frame_->subject_time());
  auto f0_downstream = Fixed::FromRaw(presentation_time_to_frac_frame()->subject_time());

  internal_frame_offset_ =
      Fixed::FromRaw(format().frac_frames_per_ns().Scale(t0_downstream - t0_internal)) +
      f0_internal - f0_downstream;
}

std::optional<Fixed> ProducerStage::PresentationTimeToDownstreamFrame(zx::time t) {
  if (!presentation_time_to_frac_frame()) {
    return std::nullopt;
  }
  return Fixed::FromRaw(presentation_time_to_frac_frame()->Apply(t.get()));
}

std::optional<zx::time> ProducerStage::DownstreamFrameToPresentationTime(Fixed downstream_frame) {
  if (!presentation_time_to_frac_frame()) {
    return std::nullopt;
  }
  return zx::time(presentation_time_to_frac_frame()->ApplyInverse(downstream_frame.raw_value()));
}

}  // namespace media_audio
