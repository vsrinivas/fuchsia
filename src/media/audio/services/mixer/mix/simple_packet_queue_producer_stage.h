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
#include "src/media/audio/services/mixer/mix/producer_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

// A ProducerStage driven by a single-threaded packet queue. The packet queue can be mutated with
// `push` and `clear` methods. This simple ProducerStage is used in tests and also as an
// implementation detail of the more complex PacketQueueProducerStage.
class SimplePacketQueueProducerStage : public ProducerStage {
 public:
  struct Args {
    // Name of this stage.
    std::string_view name;

    // Format of this stage's output stream.
    Format format;

    // Reference clock of this stage's output stream.
    zx_koid_t reference_clock_koid;
  };

  explicit SimplePacketQueueProducerStage(Args args)
      : ProducerStage(args.name, args.format, args.reference_clock_koid) {}

  // Registers a callback to invoke when a packet underflows.
  // The duration estimates the packet's lateness relative to the system monotonic clock.
  void SetUnderflowReporter(fit::function<void(zx::duration)> underflow_reporter) {
    underflow_reporter_ = std::move(underflow_reporter);
  }

  // Clears the queue.
  void clear() { pending_packet_queue_.clear(); }

  // Returns whether the queue is empty or not.
  bool empty() const { return pending_packet_queue_.empty(); }

  // Pushes a `packet` into the queue. `fence` will be closed after the packet is fully consumed.
  void push(PacketView packet, zx::eventpair fence = zx::eventpair()) {
    pending_packet_queue_.emplace_back(packet, std::move(fence));
  }

 protected:
  // Implements `PipelineStage`. Not marked `final` so subclasses can override.
  void AdvanceSelfImpl(Fixed frame) override;
  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame,
                                 int64_t frame_count) override;

  // For access to AdvanceSelfImpl.
  friend class PacketQueueProducerStage;

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

  void ReportUnderflow(Fixed underlow_frame_count);

  std::deque<PendingPacket> pending_packet_queue_;

  size_t underflow_count_;
  fit::function<void(zx::duration)> underflow_reporter_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SIMPLE_PACKET_QUEUE_PRODUCER_STAGE_H_
