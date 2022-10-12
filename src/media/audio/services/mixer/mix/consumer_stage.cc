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
    : PipelineStage(args.name, args.format, args.reference_clock),
      pipeline_direction_(args.pipeline_direction),
      presentation_delay_(args.presentation_delay),
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
                                          : -period - upstream_delay();

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
    int64_t start_frame = FrameFromPresentationTime(t).Floor();
    const int64_t end_frame = FrameFromPresentationTime(end).Floor();
    while (start_frame < end_frame) {
      int64_t length = end_frame - start_frame;
      std::optional<Packet> packet;
      if (source_) {
        packet = source_->Read(ctx, Fixed(start_frame), length);
      }
      if (!packet) {
        writer_->WriteSilence(start_frame, length);
        break;
      }

      // SampleAndHold: frame 1.X overlaps frame 2.0, so always round up.
      auto packet_start_frame = packet->start().Ceiling();
      if (packet_start_frame > start_frame) {
        writer_->WriteSilence(start_frame, packet_start_frame - start_frame);
      }

      writer_->WriteData(packet_start_frame, packet->length(), packet->payload());
      start_frame = packet->end().Ceiling();
    }

    // Advance to the next packet.
    t = end;
  }
}

void ConsumerStage::AddSource(PipelineStagePtr source, AddSourceOptions options) {
  FX_CHECK(!source_) << "Consumer already connected to source " << source_->name();
  source_ = std::move(source);
  source_->UpdatePresentationTimeToFracFrame(presentation_time_to_frac_frame());
}

void ConsumerStage::RemoveSource(PipelineStagePtr source) {
  FX_CHECK(source_ == source) << "Consumer not connected to source " << source->name();
  // When the source is disconnected, it's effectively "stopped". Updating the timeline function to
  // "stopped" will help catch bugs where a source is accidentally Read or Advance'd while detached.
  source_->UpdatePresentationTimeToFracFrame(std::nullopt);
  source_ = nullptr;
}

void ConsumerStage::UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) {
  set_presentation_time_to_frac_frame(f);
  if (source_) {
    source_->UpdatePresentationTimeToFracFrame(f);
  }
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
        .next_stop_presentation_time = pending && !pending->second
                                           ? std::optional(pending->first.reference_time)
                                           : std::nullopt,
    };
  } else {
    internal_status_ = InternalStoppedStatus{
        .next_start_presentation_time = pending && pending->second
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
