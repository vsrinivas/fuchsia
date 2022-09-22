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
    : PipelineStage(args.name, args.format, std::move(args.reference_clock)),
      pipeline_direction_(args.pipeline_direction),
      presentation_delay_(args.presentation_delay),
      writer_(std::move(args.writer)),
      pending_commands_(std::move(args.command_queue)) {}

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
    FlushPendingCommandsUntil(t);

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
    FX_CHECK(t >= status.prior_cmd.start_presentation_time)
        << t << " < " << status.prior_cmd.start_presentation_time;

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

void ConsumerStage::FlushPendingCommandsUntil(zx::time now) {
  for (;;) {
    auto cmd_or_null = pending_commands_->peek();
    if (!cmd_or_null) {
      return;
    }

    if (auto* cmd = std::get_if<StartCommand>(&*cmd_or_null); cmd) {
      if (!ApplyStartCommand(*cmd, now)) {
        return;
      }
    } else if (auto* cmd = std::get_if<StopCommand>(&*cmd_or_null); cmd) {
      if (!ApplyStopCommand(*cmd, now)) {
        return;
      }
    } else {
      FX_LOGS(FATAL) << "unhandled Command variant";
    }

    pending_commands_->pop();
  }
}

bool ConsumerStage::ApplyStartCommand(const StartCommand& cmd, zx::time now) {
  // Sanity check ordering requirements.
  if (auto* status = std::get_if<InternalStartedStatus>(&internal_status_); status) {
    auto& prior_cmd = status->prior_cmd;
    FX_LOGS(FATAL) << "Duplicate start commands: "
                   << "prior command is {"
                   << " start_time=" << prior_cmd.start_presentation_time
                   << " start_frame=" << prior_cmd.start_frame << " },"
                   << "new command is {"
                   << " start_time=" << cmd.start_presentation_time
                   << " start_frame=" << cmd.start_frame << " }";
  } else {
    auto& prior_cmd = std::get<InternalStoppedStatus>(internal_status_).prior_cmd;
    FX_CHECK(!prior_cmd || cmd.start_presentation_time > prior_cmd->stop_presentation_time)
        << ffl::String::DecRational << "Start command out-of-ourder: "
        << "prior command is {"
        << " stop_time=" << prior_cmd->stop_presentation_time
        << " stop_frame=" << prior_cmd->stop_frame << " },"
        << "new command is {"
        << " start_time=" << cmd.start_presentation_time << " start_frame=" << cmd.start_frame
        << " }";
  }

  // If this command is in the future, just update our next start time.
  if (cmd.start_presentation_time > now) {
    std::get<InternalStoppedStatus>(internal_status_).next_start_presentation_time =
        cmd.start_presentation_time;
    return false;
  }

  internal_status_ = InternalStartedStatus{
      .prior_cmd =
          {
              .start_presentation_time = cmd.start_presentation_time,
              .start_frame = cmd.start_frame,
          },
  };
  UpdatePresentationTimeToFracFrame(TimelineFunction(Fixed(cmd.start_frame).raw_value(),
                                                     cmd.start_presentation_time.get(),
                                                     format().frac_frames_per_ns()));
  if (cmd.callback) {
    cmd.callback();
  }
  return true;
}

bool ConsumerStage::ApplyStopCommand(const StopCommand& cmd, zx::time now) {
  // Sanity check ordering requirements.
  if (auto* status = std::get_if<InternalStoppedStatus>(&internal_status_); status) {
    if (auto& prior_cmd = status->prior_cmd; prior_cmd) {
      FX_LOGS(FATAL) << ffl::String::DecRational << "Duplicate stop commands: "
                     << "prior command is { stop_frame=" << prior_cmd->stop_frame << " },"
                     << "new command is { stop_frame=" << cmd.stop_frame << " }";
    } else {
      FX_LOGS(FATAL) << ffl::String::DecRational << "Stop command without preceding start: "
                     << "command is { stop_frame=" << cmd.stop_frame << " }";
    }
  }

  auto& current_status = std::get<InternalStartedStatus>(internal_status_);
  auto& prior_cmd = current_status.prior_cmd;
  FX_CHECK(cmd.stop_frame > prior_cmd.start_frame)
      << ffl::String::DecRational << "Stop command out-of-order: "
      << "prior command is { start_frame=" << prior_cmd.start_frame << " },"
      << "new command is { stop_frame=" << cmd.stop_frame << " }";

  // Compute the effective presentation time based on the last StartCommand.
  const int64_t frames_after_start = cmd.stop_frame - prior_cmd.start_frame;
  const zx::time stop_presentation_time =
      prior_cmd.start_presentation_time +
      zx::duration(format().frames_per_ns().Inverse().Scale(frames_after_start,
                                                            TimelineRate::RoundingMode::Ceiling));

  // If this command is in the future, just update our next stop time.
  if (stop_presentation_time > now) {
    current_status.next_stop_presentation_time = stop_presentation_time;
    return false;
  }

  internal_status_ = InternalStoppedStatus{
      .prior_cmd =
          InternalStopCommand{
              .stop_presentation_time = stop_presentation_time,
              .stop_frame = cmd.stop_frame,
          },
  };
  UpdatePresentationTimeToFracFrame(std::nullopt);
  writer_->End();
  if (cmd.callback) {
    cmd.callback();
  }
  return true;
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
