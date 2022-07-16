// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/pipeline_stage.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <optional>
#include <utility>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"

namespace media_audio {

void PipelineStage::Advance(MixJobContext& ctx, Fixed frame) {
  TRACE_DURATION("audio", advance_trace_name_.c_str(), "frame", frame.Integral().Floor(),
                 "frame.frac", frame.Fraction().raw_value());
  if (AdvanceSelf(frame)) {
    // Don't advance sources unless we've advanced passed all locally-cached data. Otherwise we may
    // drop source data that is referenced by our local cache.
    AdvanceSourcesImpl(ctx, frame);
  }
}

std::optional<PipelineStage::Packet> PipelineStage::Read(MixJobContext& ctx, Fixed start_frame,
                                                         int64_t frame_count) {
  TRACE_DURATION("audio", advance_trace_name_.c_str(), "dest_frame", start_frame.Integral().Floor(),
                 "dest_frame.frac", start_frame.Fraction().raw_value(), "frame_count", frame_count);

  FX_CHECK(!is_locked_);

  // Once a frame has been consumed, it cannot be locked again, we cannot travel backwards in time.
  FX_CHECK(!next_readable_frame_ || start_frame >= *next_readable_frame_);

  // Advance this stage until `start_frame`.
  if (start_frame > Fixed(0) && (!next_readable_frame_ || *next_readable_frame_ < start_frame)) {
    AdvanceSelf(start_frame);
  }

  // Check if we can reuse the cached packet.
  if (auto out_packet = ReadFromCachedPacket(start_frame, frame_count)) {
    return out_packet;
  }
  cached_packet_ = std::nullopt;

  auto packet = ReadImpl(ctx, start_frame, frame_count);
  if (!packet) {
    Advance(ctx, start_frame + Fixed(frame_count));
    return std::nullopt;
  }
  FX_CHECK(packet->length() > 0);

  is_locked_ = true;
  if (!packet->is_cached_) {
    return packet;
  }

  cached_packet_ = std::move(packet);
  auto out_packet = ReadFromCachedPacket(start_frame, frame_count);
  FX_CHECK(out_packet);
  return out_packet;
}

PipelineStage::Packet PipelineStage::MakeCachedPacket(Fixed start_frame, int64_t frame_count,
                                                      void* payload) {
  // This packet will be stored in `cached_packet_`. It won't be returned to the `Read` caller,
  // instead we'll use `ReadFromCachedPacket` to return a proxy to this packet.
  return Packet({format_, start_frame, frame_count, payload}, /*is_cached=*/true,
                /*destructor=*/nullptr);
}

PipelineStage::Packet PipelineStage::MakeUncachedPacket(Fixed start_frame, int64_t frame_count,
                                                        void* payload) {
  return Packet({format_, start_frame, frame_count, payload}, /*is_cached=*/false,
                [this, start_frame](int64_t frames_consumed) {
                  // Unlock the stream.
                  is_locked_ = false;
                  AdvanceSelf(start_frame + Fixed(frames_consumed));
                });
}

std::optional<PipelineStage::Packet> PipelineStage::ForwardPacket(
    std::optional<Packet>&& packet, std::optional<Fixed> start_frame) {
  if (!packet) {
    return std::nullopt;
  }
  const auto packet_start = start_frame ? *start_frame : packet->start();
  return Packet(
      // Wrap the packet with a proxy so we can be notified when the packet is unlocked.
      {packet->format(), packet_start, packet->length(), packet->payload()},
      /*is_cached=*/false,
      [this, packet_start, packet = std::move(packet)](int64_t frames_consumed) mutable {
        // Unlock the stream.
        is_locked_ = false;
        // What is consumed from the proxy is also consumed from the source packet.
        packet->set_frames_consumed(frames_consumed);
        // Destroy the source packet before calling `AdvanceInternal` to ensure the source stream is
        // unlocked before it is advanced.
        packet = std::nullopt;
        AdvanceSelf(packet_start + Fixed(frames_consumed));
      });
}

bool PipelineStage::AdvanceSelf(Fixed frame) {
  FX_CHECK(!is_locked_);

  // Advance the next readable frame.
  if (next_readable_frame_ && frame <= *next_readable_frame_) {
    // Next read frame is already passed the advanced point.
    return false;
  }
  next_readable_frame_ = frame;

  if (cached_packet_ && frame < cached_packet_->end()) {
    // Cached packet is still in use.
    return false;
  }
  cached_packet_ = std::nullopt;

  AdvanceSelfImpl(frame);
  return true;
}

std::optional<PipelineStage::Packet> PipelineStage::ReadFromCachedPacket(Fixed start_frame,
                                                                         int64_t frame_count) {
  if (cached_packet_) {
    if (auto intersect = cached_packet_->IntersectionWith(start_frame, frame_count)) {
      return MakeUncachedPacket(intersect->start(), intersect->length(), intersect->payload());
    }
  }
  return std::nullopt;
}

}  // namespace media_audio
