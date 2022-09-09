// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/packet_queue_producer_stage.h"

#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <utility>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"

namespace media_audio {

PacketQueueProducerStage::PacketQueueProducerStage(Args args)
    : ProducerStage(args.name, args.format, args.reference_clock_koid),
      pending_commands_(std::move(args.command_queue)),
      pending_packets_({
          .name = args.name,
          .format = args.format,
          .reference_clock_koid = args.reference_clock_koid,
          .underflow_reporter = std::move(args.underflow_reporter),
      }) {
  FX_CHECK(pending_commands_);
}

void PacketQueueProducerStage::AdvanceSelfImpl(Fixed frame) {
  FlushPendingCommands();

  // Advance our started/stopped state to `frame`.
  FlushPendingStartAndStopUntil(frame);

  // Advance the internal frame timeline if it is started.
  if (internal_frame_offset_) {
    frame += *internal_frame_offset_;
    pending_packets_.AdvanceSelfImpl(frame);
  }
}

std::optional<PipelineStage::Packet> PacketQueueProducerStage::ReadImpl(MixJobContext& ctx,
                                                                        Fixed start_frame,
                                                                        int64_t frame_count) {
  // The first step of PipelineStage::Read is to `AdvanceSelf(start_frame)`. Hence, we've already
  // called `FlushPendingStartAndStopUntil(start_frame)`. We call `FlushPendingCommands` in case a
  // packet snuck in at the last moment.
  FlushPendingCommands();

  Fixed end_frame = start_frame + Fixed(frame_count);

  // Shrink the request to ignore instants when this Producer's internal frame timeline is stopped.
  if (presentation_time_to_internal_frac_frame_) {
    // The Producer is currently started. Shrink the request if the Producer stops before
    // `end_frame`.
    if (!pending_start_and_stop_.empty()) {
      if (auto& pss = pending_start_and_stop_.front(); *pss.downstream_frame < end_frame) {
        FX_CHECK(!pss.is_start);
        FX_CHECK(pss.downstream_frame);
        end_frame = *pss.downstream_frame;
      }
    }
  } else {
    // The Producer is currently stopped. If the Producer starts before `end_frame`, advance to that
    // starting frame.
    if (!pending_start_and_stop_.empty()) {
      if (auto& pss = pending_start_and_stop_.front(); *pss.downstream_frame < end_frame) {
        FX_CHECK(pss.is_start);
        FX_CHECK(pss.downstream_frame);
        start_frame = *pss.downstream_frame;
        FlushPendingStartAndStopUntil(start_frame);
        FX_CHECK(presentation_time_to_internal_frac_frame_);

        // Shrink the request if the Producer stops (again) before `end_frame`.
        if (!pending_start_and_stop_.empty()) {
          if (auto& pss = pending_start_and_stop_.front(); *pss.downstream_frame < end_frame) {
            FX_CHECK(!pss.is_start);
            FX_CHECK(pss.downstream_frame);
            end_frame = *pss.downstream_frame;
          }
        }
      }
    }

    // If the Producer is stopped for the entire request, return nothing.
    if (!presentation_time_to_internal_frac_frame_) {
      return std::nullopt;
    }
  }

  // Update `frame_count` to match the region where the Producer is started.
  frame_count = Fixed(end_frame - start_frame).Ceiling();

  // Translate from downstream to internal frames.
  start_frame += *internal_frame_offset_;

  // The resulting packet uses internal frames. It must be translated back to downstream frames.
  auto packet = pending_packets_.ReadImpl(ctx, start_frame, frame_count);
  if (!packet) {
    return std::nullopt;
  }
  return ForwardPacket(std::move(packet), packet->start() - *internal_frame_offset_);
}

void PacketQueueProducerStage::FlushPendingCommands() {
  for (;;) {
    auto cmd_or_null = pending_commands_->pop();
    if (!cmd_or_null) {
      break;
    }

    if (auto* cmd = std::get_if<PushPacketCommand>(&*cmd_or_null); cmd) {
      pending_packets_.push(cmd->packet, std::move(cmd->fence));

    } else if (std::holds_alternative<ClearCommand>(*cmd_or_null)) {
      // The fence is cleared when `cmd_or_null` is destructed.
      pending_packets_.clear();

    } else if (auto* cmd = std::get_if<StartCommand>(&*cmd_or_null); cmd) {
      // Sanity check ordering requirements.
      if (last_pending_start_or_stop_) {
        FX_CHECK(!last_pending_start_or_stop_->is_start &&
                 cmd->start_presentation_time > last_pending_start_or_stop_->presentation_time)
            << ffl::String::DecRational << "Start command arrived out-of-order: "
            << "prior command is {"
            << " start=" << last_pending_start_or_stop_->is_start
            << " time=" << last_pending_start_or_stop_->presentation_time
            << " frame=" << last_pending_start_or_stop_->internal_frame << " },"
            << "new command is {"
            << " start_time=" << cmd->start_presentation_time << " start_frame=" << cmd->start_frame
            << " }";
      }

      PendingStartOrStop pss{
          .is_start = true,
          .presentation_time = cmd->start_presentation_time,
          .internal_frame = cmd->start_frame,
          .downstream_frame = PresentationTimeToDownstreamFrame(cmd->start_presentation_time),
      };

      last_pending_start_or_stop_ = std::move(pss);
      pss.callback = std::move(cmd->callback);
      pending_start_and_stop_.push_back(std::move(pss));

    } else if (auto* cmd = std::get_if<StopCommand>(&*cmd_or_null); cmd) {
      // Sanity check ordering requirements.
      FX_CHECK(last_pending_start_or_stop_)
          << ffl::String::DecRational << "Stop command arrived without a preceding Start: "
          << "new command is { stop_frame = " << cmd->stop_frame << " }";
      FX_CHECK(last_pending_start_or_stop_->is_start &&
               cmd->stop_frame > last_pending_start_or_stop_->internal_frame)
          << ffl::String::DecRational << "Stop command arrived out-of-order: "
          << "prior command is {"
          << " start=" << last_pending_start_or_stop_->is_start
          << " time=" << last_pending_start_or_stop_->presentation_time
          << " frame=" << last_pending_start_or_stop_->internal_frame << " },"
          << "new command is { stop_frame=" << cmd->stop_frame << " }";

      const Fixed frames_after_start =
          cmd->stop_frame - last_pending_start_or_stop_->internal_frame;

      const zx::time presentation_time =
          last_pending_start_or_stop_->presentation_time +
          zx::duration(format().frac_frames_per_ns().Inverse().Scale(
              frames_after_start.raw_value(), TimelineRate::RoundingMode::Ceiling));

      PendingStartOrStop pss{
          .is_start = false,
          .presentation_time = presentation_time,
          .internal_frame = cmd->stop_frame,
          .downstream_frame = PresentationTimeToDownstreamFrame(presentation_time),
      };

      last_pending_start_or_stop_ = std::move(pss);
      pss.callback = std::move(cmd->callback);
      pending_start_and_stop_.push_back(std::move(pss));

    } else {
      FX_CHECK(false) << "unhandled Command variant";
    }
  }
}

void PacketQueueProducerStage::FlushPendingStartAndStopUntil(Fixed frame) {
  // Cannot be called while the downstream frame is stopped.
  FX_CHECK(presentation_time_to_frac_frame());

  bool changed = false;

  // Apply all Start and Stop commands with `downstream_frame <= frame`.
  while (!pending_start_and_stop_.empty()) {
    auto& pss = pending_start_and_stop_.front();
    if (pss.downstream_frame > frame) {
      break;
    }
    if (pss.is_start) {
      presentation_time_to_internal_frac_frame_ =
          TimelineFunction(pss.internal_frame.raw_value(), pss.presentation_time.get(),
                           format().frac_frames_per_ns());
      internal_frame_offset_ = pss.internal_frame - *pss.downstream_frame;
    } else {
      presentation_time_to_internal_frac_frame_ = std::nullopt;
      internal_frame_offset_ = std::nullopt;
    }

    pending_packets_.UpdatePresentationTimeToFracFrame(presentation_time_to_internal_frac_frame_);
    if (pss.callback) {
      pss.callback();
    }

    changed = true;
    pending_start_and_stop_.pop_front();
  }

  if (changed) {
    RecomputeInternalFrameOffset();
  }
}

void PacketQueueProducerStage::UpdatePresentationTimeToFracFrame(
    std::optional<TimelineFunction> f) {
  set_presentation_time_to_frac_frame(f);

  // Recompute values derived from `presentation_time_to_frac_frame()`.
  RecomputeInternalFrameOffset();
  for (auto& pss : pending_start_and_stop_) {
    pss.downstream_frame = PresentationTimeToDownstreamFrame(pss.presentation_time);
  }
}

void PacketQueueProducerStage::RecomputeInternalFrameOffset() {
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

std::optional<Fixed> PacketQueueProducerStage::PresentationTimeToDownstreamFrame(zx::time t) {
  if (!presentation_time_to_frac_frame()) {
    return std::nullopt;
  }
  return Fixed::FromRaw(presentation_time_to_frac_frame()->Apply(t.get()));
}

}  // namespace media_audio
