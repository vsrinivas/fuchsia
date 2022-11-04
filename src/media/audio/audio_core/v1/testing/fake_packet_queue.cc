// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/testing/fake_packet_queue.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/v1/mixer/intersect.h"

namespace media::audio::testing {

FakePacketQueue::FakePacketQueue(
    std::vector<fbl::RefPtr<Packet>> packets, const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
    std::shared_ptr<Clock> audio_clock)
    : ReadableStream("FakePacketQueue", format),
      packets_(std::move(packets)),
      timeline_function_(std::move(ref_time_to_frac_presentation_frame)),
      audio_clock_(std::move(audio_clock)) {}

std::optional<ReadableStream::Buffer> FakePacketQueue::ReadLockImpl(ReadLockContext& ctx,
                                                                    Fixed frame,
                                                                    int64_t frame_count) {
  // Find the first intersecting packet.
  for (auto& p : packets_) {
    if (p->end() <= frame) {
      continue;
    }
    auto frag = mixer::Packet{
        .start = p->start(),
        .length = p->length(),
        .payload = p->payload(),
    };
    auto isect = IntersectPacket(format(), frag, frame, frame_count);
    if (!isect) {
      return std::nullopt;
    }
    return MakeUncachedBuffer(isect->start, isect->length, isect->payload, usage_mask_, gain_db_);
  }

  return std::nullopt;
}

void FakePacketQueue::TrimImpl(Fixed frame) {
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
