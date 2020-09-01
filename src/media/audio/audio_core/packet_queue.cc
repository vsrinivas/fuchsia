// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/packet_queue.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <iomanip>

#include <ffl/string.h>

#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/audio_core/audio_object.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

// To what extent should client-side underflows be logged? (A "client-side underflow" refers to when
// all or part of a packet's data is discarded because its start timestamp has already passed.)
// For each packet queue, we will log the first underflow. For subsequent occurrences, depending on
// audio_core's logging level, we throttle how frequently these are displayed. If log_level is set
// to TRACE or SPEW, all client-side underflows are logged -- at log_level -1: VLOG TRACE -- as
// specified by kUnderflowTraceInterval. If set to INFO, we log less often, at log_level 1: INFO,
// throttling by the factor kUnderflowInfoInterval. If set to WARNING or higher, we throttle these
// even more, specified by kUnderflowErrorInterval. Note: by default we set NDEBUG builds to WARNING
// and DEBUG builds to INFO. To disable all logging of client-side underflows, set kLogUnderflow to
// false.
static constexpr bool kLogUnderflow = true;
static constexpr uint16_t kUnderflowTraceInterval = 1;
static constexpr uint16_t kUnderflowInfoInterval = 10;
static constexpr uint16_t kUnderflowErrorInterval = 100;

}  // namespace

PacketQueue::PacketQueue(Format format, AudioClock audio_clock)
    : PacketQueue(format, nullptr, std::move(audio_clock)) {}

PacketQueue::PacketQueue(Format format, fbl::RefPtr<VersionedTimelineFunction> timeline_function,
                         AudioClock audio_clock)
    : ReadableStream(std::move(format)),
      timeline_function_(std::move(timeline_function)),
      audio_clock_(std::move(audio_clock)) {}

PacketQueue::~PacketQueue() {
  pending_flush_packet_queue_.clear();
  pending_packet_queue_.clear();
  pending_flush_token_queue_.clear();
}

void PacketQueue::PushPacket(const fbl::RefPtr<Packet>& packet) {
  TRACE_DURATION("audio", "PacketQueue::PushPacket");
  std::lock_guard<std::mutex> locker(pending_mutex_);
  pending_packet_queue_.emplace_back(std::move(packet));
}

void PacketQueue::Flush(const fbl::RefPtr<PendingFlushToken>& flush_token) {
  TRACE_DURATION("audio", "PacketQueue::Flush");
  std::deque<fbl::RefPtr<Packet>> flushed_packets;

  {
    std::lock_guard<std::mutex> locker(pending_mutex_);

    flushed_ = true;

    if (processing_in_progress_) {
      // Is the sink currently mixing?  If so, the flush cannot complete until the mix operation has
      // finished.  Move the 'waiting to be rendered' packets to the back of the 'waiting to be
      // flushed queue', and append our flush token (if any) to the pending flush token queue.  The
      // sink's thread will take care of releasing these objects back to the service thread for
      // cleanup when it has finished its current job.
      for (auto& packet : pending_packet_queue_) {
        pending_flush_packet_queue_.emplace_back(std::move(packet));
      }
      pending_packet_queue_.clear();

      if (flush_token != nullptr) {
        pending_flush_token_queue_.emplace_back(std::move(flush_token));
      }

      return;
    } else {
      // If the sink is not currently mixing, then we just swap the contents the pending packet
      // queues with out local queue and release the packets in the proper order once we have left
      // the pending mutex lock.
      FX_DCHECK(pending_flush_packet_queue_.empty());
      FX_DCHECK(pending_flush_token_queue_.empty());
      flushed_packets.swap(pending_packet_queue_);
    }
  }

  // Release the packets, front to back.
  for (auto& ptr : flushed_packets) {
    ptr = nullptr;
  }
}

std::optional<ReadableStream::Buffer> PacketQueue::ReadLock(Fixed frame, size_t frame_count) {
  TRACE_DURATION("audio", "PacketQueue::ReadLock");
  std::lock_guard<std::mutex> locker(pending_mutex_);

  FX_DCHECK(!processing_in_progress_);
  if (!pending_packet_queue_.size()) {
    return std::nullopt;
  }

  // TODO(fxbug.dev/50669): Obey ReadLock API.
  processing_in_progress_ = true;
  auto& packet = pending_packet_queue_.front();
  bool is_continuous = !flushed_;
  flushed_ = false;
  return std::make_optional<ReadableStream::Buffer>(
      packet->start(), packet->length(), packet->payload(), is_continuous, usage_mask_,
      Gain::kUnityGainDb, [this](bool fully_consumed) { this->ReadUnlock(fully_consumed); });
}

void PacketQueue::ReadUnlock(bool fully_consumed) {
  TRACE_DURATION("audio", "PacketQueue::ReadUnlock");
  std::lock_guard<std::mutex> locker(pending_mutex_);

  FX_DCHECK(processing_in_progress_);
  processing_in_progress_ = false;

  // Did a flush take place while we were working?  If so release each of the packets waiting to
  // be flushed back to the service thread, then release each of the flush tokens.
  if (!pending_flush_packet_queue_.empty() || !pending_flush_token_queue_.empty()) {
    for (auto& ptr : pending_flush_packet_queue_) {
      ptr = nullptr;
    }

    for (auto& ptr : pending_flush_token_queue_) {
      ptr = nullptr;
    }

    pending_flush_packet_queue_.clear();
    pending_flush_token_queue_.clear();
    return;
  }

  // If the buffer was fully consumed, release the first packet. The queue must not be empty,
  // unless the queue was flushed between ReadLock and ReadUnlock, but that case is handled above.
  if (fully_consumed) {
    FX_DCHECK(!pending_packet_queue_.empty());
    pending_packet_queue_.pop_front();
  }
}

void PacketQueue::Trim(Fixed frame) {
  TRACE_DURATION("audio", "PacketQueue::Trim");

  std::lock_guard<std::mutex> locker(pending_mutex_);
  while (!pending_packet_queue_.empty()) {
    auto packet = pending_packet_queue_.front();
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

void PacketQueue::ReportUnderflow(Fixed frac_source_start, Fixed frac_source_mix_point,
                                  zx::duration underflow_duration) {
  TRACE_INSTANT("audio", "PacketQueue::ReportUnderflow", TRACE_SCOPE_PROCESS);
  TRACE_ALERT("audio", "audiounderflow");
  uint16_t underflow_count = std::atomic_fetch_add<uint16_t>(&underflow_count_, 1u);

  if (underflow_reporter_) {
    auto fixed_to_ref_time = timeline_function_->get().first.Inverse();
    auto start_ref_time = zx::time(fixed_to_ref_time.Apply(frac_source_start.raw_value()));
    auto start_mono_time = audio_clock_.MonotonicTimeFromReferenceTime(start_ref_time);
    underflow_reporter_(start_mono_time, start_mono_time + underflow_duration);
  }

  if constexpr (kLogUnderflow) {
    auto underflow_msec = static_cast<double>(underflow_duration.to_nsecs()) / ZX_MSEC(1);
    if ((kUnderflowErrorInterval > 0) && (underflow_count % kUnderflowErrorInterval == 0)) {
      FX_LOGS(ERROR) << "PACKET QUEUE UNDERFLOW #" << underflow_count + 1 << " (1/"
                     << kUnderflowErrorInterval << "): source-start "
                     << ffl::Format(frac_source_start).c_str() << " missed mix-point "
                     << ffl::Format(frac_source_mix_point).c_str() << " by " << std::setprecision(4)
                     << underflow_msec << " ms";

    } else if ((kUnderflowInfoInterval > 0) && (underflow_count % kUnderflowInfoInterval == 0)) {
      FX_LOGS(ERROR) << "PACKET QUEUE UNDERFLOW #" << underflow_count + 1 << " (1/"
                     << kUnderflowInfoInterval << "): source-start "
                     << ffl::Format(frac_source_start).c_str() << " missed mix-point "
                     << ffl::Format(frac_source_mix_point).c_str() << " by " << std::setprecision(4)
                     << underflow_msec << " ms";

    } else if ((kUnderflowTraceInterval > 0) && (underflow_count % kUnderflowTraceInterval == 0)) {
      FX_LOGS(TRACE) << "PACKET QUEUE UNDERFLOW #" << underflow_count + 1 << " (1/"
                     << kUnderflowTraceInterval << "): source-start "
                     << ffl::Format(frac_source_start).c_str() << " missed mix-point "
                     << ffl::Format(frac_source_mix_point).c_str() << " by " << std::setprecision(4)
                     << underflow_msec << " ms";
    }
  }
}

void PacketQueue::ReportPartialUnderflow(Fixed frac_source_offset, int64_t dest_mix_offset) {
  TRACE_INSTANT("audio", "PacketQueue::ReportPartialUnderflow", TRACE_SCOPE_PROCESS);

  // Shifts by less than four source frames do not necessarily indicate underflow. A shift of this
  // duration can be caused by the round-to-nearest-dest-frame step, when our rate-conversion ratio
  // is sufficiently large (it can be as large as 4:1).
  if (frac_source_offset >= 4) {
    auto partial_underflow_count = std::atomic_fetch_add<uint16_t>(&partial_underflow_count_, 1u);
    if constexpr (kLogUnderflow) {
      if ((kUnderflowErrorInterval > 0) &&
          (partial_underflow_count % kUnderflowErrorInterval == 0)) {
        FX_LOGS(WARNING) << "PACKET QUEUE SHIFT #" << partial_underflow_count + 1 << " (1/"
                         << kUnderflowErrorInterval << "): shifted by "
                         << ffl::Format(frac_source_offset).c_str() << " source frames and "
                         << dest_mix_offset << " mix (output) frames";
      } else if ((kUnderflowInfoInterval > 0) &&
                 (partial_underflow_count % kUnderflowInfoInterval == 0)) {
        FX_LOGS(INFO) << "PACKET QUEUE SHIFT #" << partial_underflow_count + 1 << " (1/"
                      << kUnderflowInfoInterval << "): shifted by "
                      << ffl::Format(frac_source_offset).c_str() << " source frames and "
                      << dest_mix_offset << " mix (output) frames";
      } else if ((kUnderflowTraceInterval > 0) &&
                 (partial_underflow_count % kUnderflowTraceInterval == 0)) {
        FX_LOGS(TRACE) << "PACKET QUEUE SHIFT #" << partial_underflow_count + 1 << " (1/"
                       << kUnderflowTraceInterval << "): shifted by "
                       << ffl::Format(frac_source_offset).c_str() << " source frames and "
                       << dest_mix_offset << " mix (output) frames";
      }
    }
  } else {
    if constexpr (kLogUnderflow) {
      FX_LOGS(TRACE) << "shifted " << dest_mix_offset
                     << " mix (output) frames to align with source packet";
    }
  }
}

}  // namespace media::audio
