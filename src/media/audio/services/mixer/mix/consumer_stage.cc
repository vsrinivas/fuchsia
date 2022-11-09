// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/consumer_stage.h"

#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <utility>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"

namespace media_audio {

ConsumerStage::ConsumerStage(Args args)
    : BaseConsumerStage({
          .name = args.name,
          .format = args.format,
          .reference_clock = args.reference_clock,
          .thread = std::move(args.thread),
          .writer = args.writer,
      }),
      pipeline_direction_(args.pipeline_direction),
      writer_(std::move(args.writer)),
      pending_start_stop_command_(std::move(args.pending_start_stop_command)),
      start_stop_control_(args.format, args.media_ticks_per_ns, args.reference_clock) {}

ConsumerStage::Status ConsumerStage::RunMixJob(MixJobContext& ctx,
                                               const zx::time mix_job_start_time,
                                               const zx::duration period) {
  // There must be at least one full frame per period.
  FX_CHECK(format().integer_frames_per(period, ::media::TimelineRate::RoundingMode::Floor) > 0)
      << "Invalid period=" << period;

  // Time must advance.
  FX_CHECK(!last_mix_job_end_time_ || mix_job_start_time >= *last_mix_job_end_time_)
      << "RunMixJob time went backwards: " << *last_mix_job_end_time_ << " -> "
      << mix_job_start_time.get();
  last_mix_job_end_time_ = mix_job_start_time + period;

  // Output pipelines consume data that will be presented in the future.
  // Input pipelines consume data that was presented in the past.
  const zx::duration consume_offset = (pipeline_direction_ == PipelineDirection::kOutput)
                                          ? period + downstream_delay()
                                          : -period - upstream_delay_for_source();

  const zx::time start_consume_time = mix_job_start_time + consume_offset;
  const zx::time end_consume_time = start_consume_time + period;

  // Each iteration produces at most one packet for `writer_`.
  // We iterate multiple times if the consumer stops within this mix period.
  auto t = start_consume_time;
  for (;;) {
    FX_CHECK(t <= end_consume_time) << t << " > " << end_consume_time;
    UpdateStatus(ctx, t);

    if (t == end_consume_time) {
      return ToStatus(internal_status_, consume_offset);
    }

    // If stopped, advance to the next start time or the end of this job, whichever comes first.
    if (auto* status = std::get_if<InternalStoppedStatus>(&internal_status_); status) {
      if (status->next_start_presentation_time) {
        t = std::min(end_consume_time, *status->next_start_presentation_time);
      } else {
        t = end_consume_time;
      }
      continue;
    }

    // We must be started at t. Clamp early if we stop before `end_consume_time`.
    auto& status = std::get<InternalStartedStatus>(internal_status_);
    auto end = end_consume_time;
    if (status.next_stop_presentation_time) {
      end = std::min(end, *status.next_stop_presentation_time);
    }

    // Write enough packets (or silence) to fill the time interval `[t, end)`.
    const int64_t start_frame = FrameFromPresentationTime(t).Floor();
    const int64_t end_frame = FrameFromPresentationTime(end).Floor();
    CopyFromSource(ctx, start_frame, end_frame - start_frame);

    // Advance to the next packet.
    t = end;
  }
}

void ConsumerStage::set_downstream_delay(zx::duration delay) {
  FX_CHECK(pipeline_direction_ == PipelineDirection::kOutput);
  presentation_delay_ = delay;
}

void ConsumerStage::set_upstream_delay_for_source(zx::duration delay) {
  FX_CHECK(pipeline_direction_ == PipelineDirection::kInput);
  presentation_delay_ = delay;
}

void ConsumerStage::UpdateStatus(const MixJobContext& ctx,
                                 zx::time mix_job_current_presentation_time) {
  // Pop the pending command, if any.
  if (auto cmd_or_null = pending_start_stop_command_->pop(); cmd_or_null) {
    if (std::holds_alternative<StartCommand>(*cmd_or_null)) {
      start_stop_control_.Start(std::move(std::get<StartCommand>(*cmd_or_null)));
    } else {
      start_stop_control_.Stop(std::move(std::get<StopCommand>(*cmd_or_null)));
    }
  }

  // Advance to the current consume position and update our status.
  start_stop_control_.AdvanceTo(ctx.clocks(), mix_job_current_presentation_time);
  auto pending = start_stop_control_.PendingCommand(ctx.clocks());

  if (start_stop_control_.is_started()) {
    internal_status_ = InternalStartedStatus{
        .next_stop_presentation_time =
            pending && pending->second == StartStopControl::CommandType::kStop
                ? std::optional(pending->first.reference_time)
                : std::nullopt,
    };
  } else {
    internal_status_ = InternalStoppedStatus{
        .next_start_presentation_time =
            pending && pending->second == StartStopControl::CommandType::kStart
                ? std::optional(pending->first.reference_time)
                : std::nullopt,
    };
  }

  // Additional updates when the timeline changes.
  if (auto f = start_stop_control_.presentation_time_to_frac_frame();
      f != presentation_time_to_frac_frame()) {
    UpdatePresentationTimeToFracFrame(f);
    if (!start_stop_control_.is_started()) {
      writer_->End();
    }
  }
}

// static
ConsumerStage::Status ConsumerStage::ToStatus(const InternalStatus& internal_status,
                                              zx::duration consume_offset) {
  if (std::holds_alternative<InternalStartedStatus>(internal_status)) {
    return StartedStatus{};
  }

  auto& status = std::get<InternalStoppedStatus>(internal_status);
  if (!status.next_start_presentation_time) {
    return StoppedStatus{};
  }
  return StoppedStatus{
      .next_mix_job_start_time = *status.next_start_presentation_time - consume_offset,
  };
}

}  // namespace media_audio
