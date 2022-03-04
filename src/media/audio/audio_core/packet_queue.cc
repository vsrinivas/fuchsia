// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/packet_queue.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <iomanip>

#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/mixer/intersect.h"
#include "src/media/audio/lib/clock/audio_clock.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio {
namespace {

// To what extent should client-side underflows be logged? (A "client-side underflow" refers to when
// all or part of a packet's data is discarded because its start timestamp has already passed.)
// For each packet queue, we will log the first underflow. For subsequent occurrences, depending on
// audio_core's logging level, we throttle how frequently these are displayed. If log_level is set
// to TRACE or DEBUG, all client-side underflows are logged -- at log_level -1: VLOG TRACE -- as
// specified by kUnderflowTraceInterval. If set to INFO, we log less often, at log_level 1: INFO,
// throttling by the factor kUnderflowInfoInterval. If set to WARNING or higher, we throttle these
// even more, specified by kUnderflowWarningInterval. Note: by default we set NDEBUG builds to
// WARNING and DEBUG builds to INFO. To disable all logging of client-side underflows, set
// kLogUnderflow to false.
static constexpr bool kLogUnderflow = true;
static constexpr uint16_t kUnderflowTraceInterval = 1;
static constexpr uint16_t kUnderflowInfoInterval = 10;
static constexpr uint16_t kUnderflowWarningInterval = 100;

}  // namespace

PacketQueue::PacketQueue(Format format, std::unique_ptr<AudioClock> audio_clock)
    : PacketQueue(format, nullptr, std::move(audio_clock)) {}

PacketQueue::PacketQueue(Format format, fbl::RefPtr<VersionedTimelineFunction> timeline_function,
                         std::unique_ptr<AudioClock> audio_clock)
    : ReadableStream("PacketQueue", std::move(format)),
      timeline_function_(std::move(timeline_function)),
      audio_clock_(std::move(audio_clock)) {}

void PacketQueue::PushPacket(const fbl::RefPtr<Packet>& packet) {
  TRACE_DURATION("audio", "PacketQueue::PushPacket");
  std::lock_guard<std::mutex> locker(pending_mutex_);
  pending_packet_queue_.push_back({
      .packet = packet,
      .seen_in_read_lock = false,
  });
}

void PacketQueue::Flush(const fbl::RefPtr<PendingFlushToken>& flush_token) {
  TRACE_DURATION("audio", "PacketQueue::Flush");
  std::lock_guard<std::mutex> locker(pending_mutex_);

  if (read_lock_in_progress_) {
    // Is the sink currently mixing? If so, the flush cannot complete until the mix operation has
    // finished. Move the 'waiting to be rendered' packets to the back of the 'waiting to be
    // flushed queue', and append our flush token (if any) to the pending flush token queue. The
    // sink's thread will take care of releasing these objects back to the service thread for
    // cleanup when it has finished its current job.
    for (auto& pp : pending_packet_queue_) {
      pending_flush_packet_queue_.emplace_back(std::move(pp.packet));
    }
    if (flush_token != nullptr) {
      pending_flush_token_queue_.emplace_back(flush_token);
    }
  } else {
    // Release packets in order.
    FX_CHECK(pending_flush_packet_queue_.empty());
    FX_CHECK(pending_flush_token_queue_.empty());
    for (auto& pp : pending_packet_queue_) {
      pp.packet = nullptr;
    }
  }

  // Flush clears this queue.
  pending_packet_queue_.clear();
}

std::optional<ReadableStream::Buffer> PacketQueue::ReadLockImpl(ReadLockContext& ctx, Fixed frame,
                                                                int64_t frame_count) {
  std::lock_guard<std::mutex> locker(pending_mutex_);

  if (read_lock_in_progress_) {
    FX_CHECK(false) << "PacketQueue::ReadLockImpl called while read lock still held";
  }

  // Since ReadLock never goes backwards in time, we can safely trim packets before `frame`.
  // If the packet starts before the requested frame and has not been seen before, it underflowed.
  while (!pending_packet_queue_.empty()) {
    auto& pp = pending_packet_queue_.front();
    if (Fixed diff = frame - pp.packet->start(); !pp.seen_in_read_lock && diff >= Fixed(1)) {
      ReportUnderflow(pp.packet, diff);
    }
    if (pp.packet->end() > frame) {
      pp.seen_in_read_lock = true;
      break;
    }
    pending_packet_queue_.pop_front();
  }

  // Skip if there are no packets
  if (pending_packet_queue_.empty()) {
    return std::nullopt;
  }

  // Check if the requested range intersects the first packet.
  // If not the first packet must be include at least one frame >= `frame`.
  auto& packet = pending_packet_queue_.front().packet;
  auto frag = mixer::Packet{
      .start = packet->start(),
      .length = packet->length(),
      .payload = packet->payload(),
  };
  auto isect = IntersectPacket(format(), frag, frame, frame_count);
  if (!isect) {
    return std::nullopt;
  }

  read_lock_in_progress_ = true;

  // Don't use a cached buffer. We don't need caching since we don't generate any
  // data dynamically.
  //
  // IMPORTANT: Another important reason to use MakeUncachedBuffer is that caching can
  // make us hold onto packets for an unreasonably long time. Consider this example:
  //
  //    1. Client inserts a packet into the PacketQueue
  //    2. A downstream pipeline stage calls PacketQueue::ReadLock and partially consumes the packet
  //    3. Client pauses the audio stream
  //    4. Client discards all packets from the PacketQueue
  //
  // In step 4, we cannot discard the packet because a downstream pipeline stage still has
  // a reference to the packet (step 2) and will keep holding that reference until ReadLock
  // advances, which won't happen until the audio stream is unpaused (step 3), which may take
  // an arbitrarily long time. Hence it may take an arbitrarily long time to release the
  // packet. The simplest way to avoid this problem is to not use cached buffers.
  return MakeUncachedBuffer(isect->start, isect->length, isect->payload, usage_mask_,
                            Gain::kUnityGainDb);
}

void PacketQueue::TrimImpl(Fixed frame) {
  std::lock_guard<std::mutex> locker(pending_mutex_);

  if (read_lock_in_progress_) {
    // This Trim is ending a prior ReadLock.
    read_lock_in_progress_ = false;

    // Did a flush take place while we were working?  If so release each of the packets waiting to
    // be flushed back to the service thread, then release each of the flush tokens.
    for (auto& ptr : pending_flush_packet_queue_) {
      ptr = nullptr;
    }
    for (auto& ptr : pending_flush_token_queue_) {
      ptr = nullptr;
    }
    pending_flush_packet_queue_.clear();
    pending_flush_token_queue_.clear();
  }

  // Release packets that end before our trim position.
  while (!pending_packet_queue_.empty()) {
    auto packet = pending_packet_queue_.front().packet;
    if (packet->end() > frame) {
      return;
    }
    pending_packet_queue_.pop_front();
  }
}

BaseStream::TimelineFunctionSnapshot PacketQueue::ref_time_to_frac_presentation_frame() const {
  if (!timeline_function_) {
    return {
        .timeline_function = TimelineFunction(),
        .generation = kInvalidGenerationId,
    };
  }
  auto [timeline_function, generation] = timeline_function_->get();

  return {
      .timeline_function = timeline_function,
      .generation = generation,
  };
}

void PacketQueue::ReportUnderflow(const fbl::RefPtr<Packet>& packet, Fixed underflow_frames) {
  TRACE_INSTANT("audio", "PacketQueue::UNDERFLOW", TRACE_SCOPE_THREAD, "underflow_frames",
                underflow_frames.Floor(), "underflow_frames.frac",
                underflow_frames.Fraction().raw_value());
  TRACE_ALERT("audio", "audiounderflow");
  underflow_count_++;

  // We estimate the underflow duration using the stream's frame rate.
  // This can be an underestimate in three ways:
  //
  //   * If the stream has been paused, this does not include the time spent paused.
  //
  //   * Frames are typically read in batches. This does not account for the batch size.
  //     In practice we expect the batch size should be 10ms or less, which puts a bound
  //     on this underestimate.
  //
  //   * `underflow_frames` is ultimately derived from the PacketQueue's reference clock.
  //     For example, if the reference clock is running slower than the system monotonic
  //     clock, then the underflow will appear shorter than it actually was. This error is
  //     bounded by the maximum rate difference of the reference clock, which is +/-0.1%
  //     (see zx_clock_update).
  //
  auto duration =
      zx::duration(format().frames_per_ns().Inverse().Scale(underflow_frames.Ceiling()));

  if (underflow_reporter_) {
    underflow_reporter_(duration);
  }

  if constexpr (kLogUnderflow) {
    auto underflow_msec = static_cast<double>(duration.to_nsecs()) / ZX_MSEC(1);

#define LOG_UNDERFLOW(where, interval)                                                   \
  FX_LOGS(where) << "PACKET QUEUE UNDERFLOW #" << underflow_count_ << " (1/" << interval \
                 << "): packet [" << ffl::String::DecRational << packet->start() << ", " \
                 << packet->end() << "] arrived late by " << underflow_msec << " ms ("   \
                 << underflow_frames << " frames)"

    if ((kUnderflowWarningInterval > 0) &&
        ((underflow_count_ - 1) % kUnderflowWarningInterval == 0)) {
      LOG_UNDERFLOW(WARNING, kUnderflowWarningInterval);

    } else if ((kUnderflowInfoInterval > 0) &&
               ((underflow_count_ - 1) % kUnderflowInfoInterval == 0)) {
      LOG_UNDERFLOW(INFO, kUnderflowInfoInterval);

    } else if ((kUnderflowTraceInterval > 0) &&
               ((underflow_count_ - 1) % kUnderflowTraceInterval == 0)) {
      LOG_UNDERFLOW(TRACE, kUnderflowTraceInterval);
    }
  }
}

}  // namespace media::audio
