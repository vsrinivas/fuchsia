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
    : PipelineStage(args.name, args.format, std::move(args.reference_clock)),
      internal_source_(std::move(args.internal_source)),
      pending_commands_(std::move(args.command_queue)) {
  FX_CHECK(internal_source_);
  FX_CHECK(internal_source_->format() == format());
  FX_CHECK(internal_source_->reference_clock() == reference_clock());
  FX_CHECK(pending_commands_);
}

void ProducerStage::AdvanceSelfImpl(Fixed frame) {
  // Apply all Start and Stop commands through `frame`.
  for (;;) {
    auto cmd = NextCommand();
    if (!cmd || cmd->downstream_frame > frame) {
      break;
    }
    ApplyNextCommand(*cmd);
  }
}

void ProducerStage::AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) {
  // Advance the internal frame timeline if it is started.
  if (internal_frame_offset_) {
    internal_source_->Advance(ctx, frame + *internal_frame_offset_);
  }
}

std::optional<PipelineStage::Packet> ProducerStage::ReadImpl(MixJobContext& ctx, Fixed start_frame,
                                                             int64_t frame_count) {
  Fixed end_frame = start_frame + Fixed(frame_count);

  // Shrink the request to ignore instants when this producer's internal frame timeline is stopped.
  // The first step of PipelineStage::Read is to `AdvanceSelf(start_frame)`. Hence, we've already
  // applied all pending Start and Stop command up through `start_frame`.
  if (presentation_time_to_internal_frac_frame_) {
    // The producer is currently started. Shrink the request if the producer stops before
    // `end_frame`.
    if (auto cmd = NextCommand(); cmd && !cmd->is_start && cmd->downstream_frame < end_frame) {
      end_frame = cmd->downstream_frame;
    }
  } else {
    // The producer is currently stopped. If the producer starts before `end_frame`, advance to that
    // starting frame.
    if (auto cmd = NextCommand(); cmd && cmd->is_start && cmd->downstream_frame < end_frame) {
      start_frame = cmd->downstream_frame;
      ApplyNextCommand(*cmd);

      // Shrink the request if the Producer stops (again) before `end_frame`.
      if (auto cmd = NextCommand(); cmd && !cmd->is_start && cmd->downstream_frame < end_frame) {
        end_frame = cmd->downstream_frame;
      }
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

std::optional<ProducerStage::CommandSummary> ProducerStage::NextCommand() {
  // Cannot be called while the downstream timeline is stopped.
  FX_CHECK(presentation_time_to_frac_frame());

  auto cmd_or_null = pending_commands_->peek();
  if (!cmd_or_null) {
    return std::nullopt;
  }

  if (auto* cmd = std::get_if<StartCommand>(&*cmd_or_null); cmd) {
    // Sanity check ordering requirements.
    if (last_command_) {
      FX_CHECK(!last_command_->is_start &&
               cmd->start_presentation_time > last_command_->presentation_time)
          << ffl::String::DecRational << "Start command arrived out-of-order: "
          << "prior command is {"
          << " start=" << last_command_->is_start          //
          << " time=" << last_command_->presentation_time  //
          << " frame=" << last_command_->internal_frame << " },"
          << "new command is {"
          << " start_time=" << cmd->start_presentation_time  //
          << " start_frame=" << cmd->start_frame << " }";
    }

    return CommandSummary{
        .is_start = true,
        .presentation_time = cmd->start_presentation_time,
        .internal_frame = cmd->start_frame,
        .downstream_frame = *PresentationTimeToDownstreamFrame(cmd->start_presentation_time),
    };

  } else if (auto* cmd = std::get_if<StopCommand>(&*cmd_or_null); cmd) {
    // Sanity check ordering requirements.
    FX_CHECK(last_command_) << ffl::String::DecRational
                            << "Stop command arrived without a preceding Start: "
                            << "new command is { stop_frame = " << cmd->stop_frame << " }";
    FX_CHECK(last_command_->is_start && cmd->stop_frame > last_command_->internal_frame)
        << ffl::String::DecRational << "Stop command arrived out-of-order: "
        << "prior command is {"
        << " start=" << last_command_->is_start          //
        << " time=" << last_command_->presentation_time  //
        << " frame=" << last_command_->internal_frame << " },"
        << "new command is { stop_frame=" << cmd->stop_frame << " }";

    const Fixed frames_after_start = cmd->stop_frame - last_command_->internal_frame;

    const zx::time presentation_time =
        last_command_->presentation_time +
        zx::duration(format().frac_frames_per_ns().Inverse().Scale(
            frames_after_start.raw_value(), TimelineRate::RoundingMode::Ceiling));

    return CommandSummary{
        .is_start = false,
        .presentation_time = presentation_time,
        .internal_frame = cmd->stop_frame,
        .downstream_frame = *PresentationTimeToDownstreamFrame(presentation_time),
    };

  } else {
    UNREACHABLE << "unhandled Command variant";
  }
}

// Applies `cmd`, which must be a summary of the first command in `pending_commands_`, then pops the
// command from `pending_commands_`.
void ProducerStage::ApplyNextCommand(const CommandSummary& cmd) {
  if (cmd.is_start) {
    presentation_time_to_internal_frac_frame_ = TimelineFunction(
        cmd.internal_frame.raw_value(), cmd.presentation_time.get(), format().frac_frames_per_ns());
    internal_frame_offset_ = cmd.internal_frame - cmd.downstream_frame;
  } else {
    presentation_time_to_internal_frac_frame_ = std::nullopt;
    internal_frame_offset_ = std::nullopt;
  }

  auto popped_cmd = pending_commands_->pop();
  FX_CHECK(popped_cmd);
  if (cmd.is_start) {
    if (auto start = std::get<StartCommand>(*popped_cmd); start.callback) {
      start.callback();
    }
  } else {
    if (auto stop = std::get<StopCommand>(*popped_cmd); stop.callback) {
      stop.callback();
    }
  }

  internal_source_->UpdatePresentationTimeToFracFrame(presentation_time_to_internal_frac_frame_);
  last_command_ = cmd;
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

}  // namespace media_audio
