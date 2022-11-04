// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PACKET_QUEUE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PACKET_QUEUE_H_

#include <deque>
#include <memory>
#include <mutex>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/packet.h"
#include "src/media/audio/audio_core/v1/pending_flush_token.h"
#include "src/media/audio/audio_core/v1/stream.h"
#include "src/media/audio/audio_core/v1/versioned_timeline_function.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio {

class PacketQueue : public ReadableStream {
 public:
  PacketQueue(Format format, std::shared_ptr<Clock> audio_clock);
  PacketQueue(Format format,
              fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
              std::shared_ptr<Clock> audio_clock);

  bool empty() const {
    std::lock_guard<std::mutex> locker(pending_mutex_);
    return pending_packet_queue_.empty();
  }

  void set_usage(const StreamUsage& usage) {
    usage_mask_.clear();
    usage_mask_.insert(usage);
  }

  void PushPacket(const fbl::RefPtr<Packet>& packet);
  void Flush(const fbl::RefPtr<PendingFlushToken>& flush_token = nullptr);

  // Register a callback to invoke when a packet underflows.
  // The duration estimates the lateness of the packet relative to the system monotonic clock.
  void SetUnderflowReporter(fit::function<void(zx::duration)> underflow_reporter) {
    underflow_reporter_ = std::move(underflow_reporter);
  }

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  std::shared_ptr<Clock> reference_clock() override { return audio_clock_; }

 private:
  // |media::audio::ReadableStream|
  std::optional<ReadableStream::Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed frame,
                                                     int64_t frame_count) override;
  void TrimImpl(Fixed frame) override;
  void ReadUnlock() override;

  void ReportUnderflow(const fbl::RefPtr<Packet>& packet, Fixed underflow_frames)
      FXL_REQUIRE(pending_mutex_);

  StreamUsageMask usage_mask_;

  mutable std::mutex pending_mutex_;

  struct PendingPacket {
    fbl::RefPtr<Packet> packet;
    bool seen_in_read_lock = false;
  };

  // New packets go on `pending_packet_queue_`.
  //
  // If a Flush happens while a ReadLock is held, then a downstream stage has a
  // non-reference-counted pointer to the first packet in `pending_packet_queue_`.
  // We can't flush that packet until the ReadLock is released.
  //
  // Hence, if Flush happens while in a ReadLock, we move all pending packets to
  // `pending_flush_packet_queue_` and add a flush token to `pending_flush_token_queue_`.
  // After the ReadLock is released, we remove all packets and flush tokens from these
  // queues. Each `PendingFlushToken` completes a DiscardAllPackets FIDL call when the
  // token is destructed, so as each token is removed from the queue, a DiscardAllPackets
  // FIDL call is completed.
  //
  // If a Flush happens while a ReadLock is not held, it can be serviced immediately; the
  // pending flush queues are not used.
  std::deque<PendingPacket> pending_packet_queue_ FXL_GUARDED_BY(pending_mutex_);
  std::deque<fbl::RefPtr<Packet>> pending_flush_packet_queue_ FXL_GUARDED_BY(pending_mutex_);
  std::deque<fbl::RefPtr<PendingFlushToken>> pending_flush_token_queue_
      FXL_GUARDED_BY(pending_mutex_);
  bool read_lock_in_progress_ FXL_GUARDED_BY(pending_mutex_) = false;

  size_t underflow_count_ FXL_GUARDED_BY(pending_mutex_) = {0};
  fit::function<void(zx::duration)> underflow_reporter_;

  fbl::RefPtr<VersionedTimelineFunction> timeline_function_;
  std::shared_ptr<Clock> audio_clock_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_PACKET_QUEUE_H_
