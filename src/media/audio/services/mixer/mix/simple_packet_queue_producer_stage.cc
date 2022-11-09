// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"

#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <utility>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"

namespace media_audio {

SimplePacketQueueProducerStage::SimplePacketQueueProducerStage(Args args)
    : PipelineStage(args.name, args.format, std::move(args.reference_clock),
                    std::move(args.initial_thread)),
      pending_commands_(std::move(args.command_queue)),
      underflow_reporter_(std::move(args.underflow_reporter)) {}

bool SimplePacketQueueProducerStage::empty() const {
  FX_CHECK(!pending_commands_);
  return pending_packet_queue_.empty();
}

void SimplePacketQueueProducerStage::push(PacketView packet, zx::eventpair fence) {
  FX_CHECK(!pending_commands_);
  pending_packet_queue_.emplace_back(packet, 0, std::move(fence));
}

void SimplePacketQueueProducerStage::UpdatePresentationTimeToFracFrame(
    std::optional<TimelineFunction> f) {
  set_presentation_time_to_frac_frame(f);
}

void SimplePacketQueueProducerStage::AdvanceSelfImpl(Fixed frame) {
  FlushPendingCommands();

  while (!pending_packet_queue_.empty()) {
    const auto& pending_packet = pending_packet_queue_.front();
    if (pending_packet.end_frame() > frame) {
      return;
    }
    pending_packet_queue_.pop_front();
  }
}

std::optional<PipelineStage::Packet> SimplePacketQueueProducerStage::ReadImpl(MixJobContext& ctx,
                                                                              Fixed start_frame,
                                                                              int64_t frame_count) {
  FlushPendingCommands();

  // Clean up pending packets before `start_frame`.
  while (!pending_packet_queue_.empty()) {
    auto& pending_packet = pending_packet_queue_.front();
    // If the packet starts before the requested frame and has not been seen before, it underflowed.
    if (const Fixed underflow_frame_count = start_frame - pending_packet.start_frame();
        !pending_packet.seen_in_read_ && underflow_frame_count >= Fixed(1)) {
      ReportUnderflow(underflow_frame_count);
    }
    if (pending_packet.end_frame() > start_frame) {
      pending_packet.seen_in_read_ = true;
      break;
    }
    pending_packet_queue_.pop_front();
  }

  if (pending_packet_queue_.empty()) {
    return std::nullopt;
  }

  // Read the next pending packet.
  const auto& pending_packet = pending_packet_queue_.front();
  if (const auto intersect = pending_packet.IntersectionWith(start_frame, frame_count)) {
    // We don't need to cache the returned packet, since we don't generate any data dynamically.
    return MakeUncachedPacket(intersect->start_frame(), intersect->frame_count(),
                              intersect->payload());
  }
  return std::nullopt;
}

void SimplePacketQueueProducerStage::FlushPendingCommands() {
  if (!pending_commands_) {
    return;
  }

  for (;;) {
    auto cmd_or_null = pending_commands_->pop();
    if (!cmd_or_null) {
      break;
    }

    if (auto* cmd = std::get_if<PushPacketCommand>(&*cmd_or_null); cmd) {
      // Skip if this segment has already been released.
      if (released_before_segment_id_ && cmd->segment_id < *released_before_segment_id_) {
        continue;
      }
      pending_packet_queue_.emplace_back(cmd->packet, cmd->segment_id, std::move(cmd->fence));

    } else if (auto* cmd = std::get_if<ReleasePacketsCommand>(&*cmd_or_null); cmd) {
      auto start = pending_packet_queue_.begin();
      auto end = start;
      for (; end != pending_packet_queue_.end(); end++) {
        if (end->segment_id() >= cmd->before_segment_id) {
          break;
        }
      }
      pending_packet_queue_.erase(start, end);
      released_before_segment_id_ = cmd->before_segment_id;

    } else {
      UNREACHABLE << "unhandled Command variant";
    }
  }
}

void SimplePacketQueueProducerStage::ReportUnderflow(Fixed underlow_frame_count) {
  ++underflow_count_;
  if (underflow_reporter_) {
    // We estimate the underflow duration using the stream's frame rate. However, this can be an
    // underestimate in three ways:
    //
    // * If the stream has been paused, this does not include the time spent paused.
    //
    // * Frames are typically read in batches. This does not account for the batch size. In practice
    //   we expect the batch size should be 10ms or less, which puts a bound on this underestimate.
    //
    // * `underflow_frame_count` is ultimately derived from the reference clock of the stage. For
    //   example, if the reference clock is running slower than the system monotonic clock, then the
    //   underflow will appear shorter than it actually was. This error is bounded by the maximum
    //   rate difference of the reference clock, which is +/-0.1% (see `zx_clock_update`).
    const auto duration =
        zx::duration(format().frames_per_ns().Inverse().Scale(underlow_frame_count.Ceiling()));
    underflow_reporter_(duration);
  }
}

}  // namespace media_audio
