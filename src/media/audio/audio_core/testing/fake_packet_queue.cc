// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/fake_packet_queue.h"

#include <lib/syslog/cpp/macros.h>

namespace media::audio::testing {

FakePacketQueue::FakePacketQueue(
    std::vector<fbl::RefPtr<Packet>> packets, const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
    std::unique_ptr<AudioClock> audio_clock)
    : ReadableStream(format),
      packets_(std::move(packets)),
      timeline_function_(std::move(ref_time_to_frac_presentation_frame)),
      audio_clock_(std::move(audio_clock)) {}

std::optional<ReadableStream::Buffer> FakePacketQueue::ReadLock(ReadLockContext& ctx, Fixed frame,
                                                                int64_t frame_count) {
  Fixed frame_end = frame + Fixed(frame_count);

  // Find the first intersecting packet.
  for (auto& p : packets_) {
    if (frame_end <= p->start()) {
      break;
    }
    if (frame >= p->end()) {
      continue;
    }

    // Intersect [frame, frame_end) with [p->start(), p->end()).
    Fixed range_start = std::max(frame, p->start());
    Fixed range_end = std::min(frame_end, p->end());

    // Clip the intersection so it has an integer number of frames.
    int64_t range_frames = Fixed(range_end - range_start).Floor();

    // Compute the offset into this packet's payload buffer.
    int64_t payload_offset_frames = Fixed(range_start - p->start()).Ceiling();
    int64_t payload_offset_bytes = payload_offset_frames * format().bytes_per_frame();

    return std::make_optional<ReadableStream::Buffer>(
        range_start, range_frames, reinterpret_cast<char*>(p->payload()) + payload_offset_bytes,
        true, usage_mask_, gain_db_);
  }

  return std::nullopt;
}

void FakePacketQueue::Trim(Fixed frame) {
  while (!packets_.empty()) {
    auto& p = packets_[0];
    if (p->end() > frame) {
      return;
    }
    packets_.erase(packets_.begin());
  }
}

ReadableStream::TimelineFunctionSnapshot FakePacketQueue::ref_time_to_frac_presentation_frame()
    const {
  auto [timeline_function, generation] = timeline_function_->get();
  return {
      .timeline_function = timeline_function,
      .generation = generation,
  };
}

}  // namespace media::audio::testing
