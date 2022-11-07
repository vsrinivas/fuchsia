// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SIMPLE_PACKET_QUEUE_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SIMPLE_PACKET_QUEUE_PRODUCER_STAGE_H_

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
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

// A ProducerStage driven by a packet queue. This is a "simple" producer because it does not handle
// Start or Stop commands. This is intended to be embedded within a ProducerStage, but can also be
// used in isolation in tests.
class SimplePacketQueueProducerStage : public PipelineStage {
 public:
  struct PushPacketCommand {
    PacketView packet;
    // Closed after `packet` is fully consumed.
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
    UnreadableClock reference_clock;

    // Message queue for pending commands. Will be drained by each call to Advance or Read. If this
    // field is nullptr, the queue can be driven by calls to `clear`, `empty`, and `push` -- this is
    // primarily useful in unit tests.
    std::shared_ptr<CommandQueue> command_queue;

    // A callback to invoke when a packet underflows. Optional: can be nullptr.
    // The duration estimates the packet's lateness relative to the system monotonic clock.
    // TODO(fxbug.dev/113389): Use `fit::inline_function`.
    fit::function<void(zx::duration)> underflow_reporter;
  };

  explicit SimplePacketQueueProducerStage(Args args);

  // Implements `PipelineStage`.
  void AddSource(PipelineStagePtr source, AddSourceOptions options) final {
    UNREACHABLE << "SimplePacketQueueProducerStage should not have a source";
  }
  void RemoveSource(PipelineStagePtr source) final {
    UNREACHABLE << "SimplePacketQueueProducerStage should not have a source";
  }
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) final;

  // Clears the queue.
  //
  // REQUIRED: `Args::command_queue` was not specified.
  void clear();

  // Reports whether the queue is empty or not.
  //
  // REQUIRED: `Args::command_queue` was not specified.
  bool empty() const;

  // Pushes a `packet` into the queue. `fence` will be closed after the packet is fully consumed.
  //
  // REQUIRED: `Args::command_queue` was not specified.
  void push(PacketView packet, zx::eventpair fence = zx::eventpair());

 protected:
  // Implements `PipelineStage`.
  void AdvanceSelfImpl(Fixed frame) final;
  void AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) final {}
  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame, int64_t frame_count) final;

 private:
  class PendingPacket : public PacketView {
   public:
    PendingPacket(PacketView view, zx::eventpair fence)
        : PacketView(view), fence_(std::move(fence)) {}

    PendingPacket(PendingPacket&& rhs) = default;
    PendingPacket& operator=(PendingPacket&& rhs) = default;

    PendingPacket(const PendingPacket& rhs) = delete;
    PendingPacket& operator=(const PendingPacket& rhs) = delete;

   private:
    friend class SimplePacketQueueProducerStage;

    zx::eventpair fence_;
    bool seen_in_read_ = false;
  };

  void FlushPendingCommands();
  void ReportUnderflow(Fixed underlow_frame_count);

  const std::shared_ptr<CommandQueue> pending_commands_;
  const fit::function<void(zx::duration)> underflow_reporter_;

  std::deque<PendingPacket> pending_packet_queue_;
  size_t underflow_count_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SIMPLE_PACKET_QUEUE_PRODUCER_STAGE_H_
