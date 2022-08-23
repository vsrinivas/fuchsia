// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PACKET_QUEUE_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PACKET_QUEUE_PRODUCER_STAGE_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <lib/fpromise/result.h>
#include <lib/zx/time.h>

#include <deque>
#include <functional>
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
    // Closed after `packet` is fully consumed.
    zx::eventpair fence;
  };

  struct ClearCommand {
    // Closed after the queue is cleared. If the queue was not empty, this fence does not occur
    // until all queued packets are released.
    zx::eventpair fence;
  };

  struct StartCommand {
    // Reference timestamp at which the producer should be started.
    zx::time start_presentation_time;
    // The first frame to start producing at `start_presentation_time`.
    Fixed start_frame;
    // Callback invoked after the producer has started.
    // Optional: can be nullptr.
    std::function<void()> callback;
  };

  struct StopCommand {
    // The frame just after the last frame to produce before stopping. This must be `> start_frame`
    // of the prior StartCommand and it must be aligned with frame boundaries defined by the prior
    // StartCommand. See comment below for discussion of ordering.
    Fixed stop_frame;
    // Callback invoked after the producer has stopped.
    // Optional: can be nullptr.
    std::function<void()> callback;
  };

  // Commands can arrive in any order, except for Start and Stop, which must arrive in an
  // alternating sequence, with Start arriving first and subsequent Stop and Start commands ordered
  // by presentation time. For Start, the presentation time is specified explicitly. For Stop, the
  // presentation time is:
  //
  // ```
  // presentation_time_to_frac_frame.Inverse().Apply(stop_frame)
  // ```
  //
  // where `presentation_time_to_frac_frame` was defined by the prior Start.
  using Command = std::variant<PushPacketCommand, ClearCommand, StartCommand, StopCommand>;
  using CommandQueue = ThreadSafeQueue<Command>;

  struct Args {
    // Name of this stage.
    std::string_view name;

    // Format of this stage's output stream.
    Format format;

    // Reference clock of this stage's output stream.
    zx_koid_t reference_clock_koid;

    // Message queue for pending commands. Will be drained by each call to Advance or Read.
    std::shared_ptr<CommandQueue> command_queue;
  };

  explicit PacketQueueProducerStage(Args args);

  // Implements `PipelineStage`.
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) final;

  // Registers a callback to invoke when a packet underflows.
  // The duration estimates the packet's lateness relative to the system monotonic clock.
  void SetUnderflowReporter(std::function<void(zx::duration)> underflow_reporter) {
    pending_packets_.SetUnderflowReporter(std::move(underflow_reporter));
  }

 protected:
  // Implements `PipelineStage`.
  void AdvanceSelfImpl(Fixed frame) final;
  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame, int64_t frame_count) final;

 private:
  void FlushPendingCommands();
  void FlushPendingStartAndStopUntil(Fixed frame);
  void RecomputeInternalFrameOffset();
  std::optional<Fixed> PresentationTimeToDownstreamFrame(zx::time t);

  // This Producer stage references two frame timelines:
  //
  // * An *internal* frame timeline. This is defined relative to the Producer's media timeline, as
  //   described in ../docs/timelines.md. FIDL commands use the media timeline directly, then get
  //   translated to internal commands (PushPacket, Start, Stop) which use internal frames.
  //
  // * A *downstream* frame timeline. This is the same frame timeline used by our downstream
  //   PipelineStage. Public methods (Read, Advance, presentation_time_to_frac_frame) use the
  //   downstream frame timeline. Then, within AdvanceSelfImpl and ReadImpl, we translate downstream
  //   frames to internal frames on-the-fly.
  //
  // This design makes it simpler to implement Start and Stop with frame accuracy. For example,
  // suppose we receive Stop and Start commands that are separated by a very short duration, shorter
  // than one full mix job. To implement these commands accurately, the translation from downstream
  // to internal frames needs to use one function for all frames before the Stop and a second
  // function for all frames after the Start. It's best to do this translation internally rather
  // than force it on our downstream PipelineStage. See additional discussion in
  // ../docs/timelines.md.
  //
  // The translation between downstream frame and presentation time is stored in
  // `presentation_time_to_frac_frame()`.
  //
  // The translation between internal frame and presentation time is stored here.
  std::optional<TimelineFunction> presentation_time_to_internal_frac_frame_;

  // Given a downstream frame, we can compute an internal frame using the formula
  // `f_internal = f_downstream + internal_frame_offset_`. This is `std::nullopt` iff either the
  // downstream or internal frame timeline is stopped.
  std::optional<Fixed> internal_frame_offset_;

  // This struct represents a Start or Stop command which has been popped from `pending_commands_`
  // and is scheduled to be applied at some point in the future.
  struct PendingStartOrStop {
    // True if Start, otherwise Stop.
    bool is_start;

    // The time when this command should take effect, in three different units. The
    // `downstream_frame` is derived from the current `presentation_time_to_frac_frame()` -- it is
    // std::nullopt iff the downstream stage is stopped.
    zx::time presentation_time;
    Fixed internal_frame;
    std::optional<Fixed> downstream_frame;

    // TODO(fxbug.dev/87651): When the downstream frame timeline changes, we need to recompute
    // downstream_frame from presentation_time. For now, we assume the downstream timeline is
    // set once immediately after construction, then never changed.

    // Callback invoked after this command is applied. This is always nullptr in
    // `last_pending_start_or_stop_`.
    std::function<void()> callback;
  };

  // Asynchronous commands are received from pending_commands_. As commands are popped from this
  // queue, packet commands (PushPacket and Clear) are applied to `pending_packets_` while Start and
  // Stop commands are queued into `pending_start_and_stop_`.
  std::shared_ptr<CommandQueue> pending_commands_;
  std::deque<PendingStartOrStop> pending_start_and_stop_;
  SimplePacketQueueProducerStage pending_packets_;  // uses internal frame time

  // The last start or stop command that was added to `pending_start_and_stop_`. This field never
  // includes the `callback` (the `callback` field is always nullptr).
  std::optional<PendingStartOrStop> last_pending_start_or_stop_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PACKET_QUEUE_PRODUCER_STAGE_H_
