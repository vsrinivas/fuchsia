// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PACKET_QUEUE_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PACKET_QUEUE_PRODUCER_STAGE_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/fpromise/result.h>
#include <lib/zx/time.h>

#include <deque>
#include <optional>
#include <utility>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/common/thread_safe_queue.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"

namespace media_audio {

// A wrapper around a SimplePacketQueueProducerStage that is driven by commands from other threads.
class PacketQueueProducerStage : public ProducerStage {
 public:
  struct PushPacketCommand {
    PacketView packet;
    // Closed after the packet is fully consumed.
    zx::eventpair fence;
  };
  struct ClearCommand {
    // Closed after the queue is cleared. If the queue was not empty, this fence does not occur
    // until all queued packets are released.
    zx::eventpair fence;
  };
  using Command = std::variant<PushPacketCommand, ClearCommand>;
  using CommandQueue = ThreadSafeQueue<Command>;

  struct Args {
    // Name of this stage.
    std::string_view name;

    // Format of this stage's output stream.
    Format format;

    // Reference clock of this stage's output stream.
    zx_koid_t reference_clock_koid;

    // Message queue for pending commands. Will be drained at each call to Advance or Read.
    std::shared_ptr<CommandQueue> command_queue;
  };

  explicit PacketQueueProducerStage(Args args);

  // Registers a callback to invoke when a packet underflows.
  // The duration estimates the packet's lateness relative to the system monotonic clock.
  void SetUnderflowReporter(fit::function<void(zx::duration)> underflow_reporter) {
    queue_.SetUnderflowReporter(std::move(underflow_reporter));
  }

 protected:
  // Implements `PipelineStage`.
  void AdvanceSelfImpl(Fixed frame) final;
  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame, int64_t frame_count) final;

 private:
  void ApplyPendingCommands();

  std::shared_ptr<CommandQueue> pending_command_queue_;
  SimplePacketQueueProducerStage queue_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PACKET_QUEUE_PRODUCER_STAGE_H_
