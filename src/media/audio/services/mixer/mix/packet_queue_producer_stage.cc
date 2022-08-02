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
      pending_command_queue_(std::move(args.command_queue)),
      queue_({
          .name = args.name,
          .format = args.format,
          .reference_clock_koid = args.reference_clock_koid,
      }) {
  FX_CHECK(pending_command_queue_);
}

void PacketQueueProducerStage::AdvanceSelfImpl(Fixed frame) {
  ApplyPendingCommands();
  queue_.AdvanceSelfImpl(frame);
}

std::optional<PipelineStage::Packet> PacketQueueProducerStage::ReadImpl(MixJobContext& ctx,
                                                                        Fixed start_frame,
                                                                        int64_t frame_count) {
  ApplyPendingCommands();
  return ForwardPacket(queue_.Read(ctx, start_frame, frame_count));
}

void PacketQueueProducerStage::ApplyPendingCommands() {
  for (;;) {
    auto cmd_or_null = pending_command_queue_->pop();
    if (!cmd_or_null) {
      return;
    }
    if (auto* cmd = std::get_if<PushPacketCommand>(&*cmd_or_null); cmd) {
      queue_.push(cmd->packet, std::move(cmd->fence));
    } else if (std::holds_alternative<ClearCommand>(*cmd_or_null)) {
      // The fence is cleared when `cmd_or_null` is destructed.
      queue_.clear();
    } else {
      FX_CHECK(false) << "unhandled Command variant";
    }
  }
}

}  // namespace media_audio
