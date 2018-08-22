// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/graph/packet.h"

#include "garnet/bin/mediaplayer/graph/payload_allocator.h"
#include "lib/fxl/logging.h"

namespace media_player {

// static
PacketPtr Packet::Create(int64_t pts, media::TimelineRate pts_rate,
                         bool keyframe, bool end_of_stream,
                         fbl::RefPtr<PayloadBuffer> payload_buffer) {
  return std::make_shared<Packet>(pts, pts_rate, keyframe, end_of_stream,
                                  std::move(payload_buffer));
}

// static
PacketPtr Packet::CreateEndOfStream(int64_t pts, media::TimelineRate pts_rate) {
  return std::make_shared<Packet>(pts, pts_rate,
                                  false,     // keyframe
                                  true,      // end_of_stream
                                  nullptr);  // payload_buffer
}

Packet::Packet(int64_t pts, media::TimelineRate pts_rate, bool keyframe,
               bool end_of_stream, fbl::RefPtr<PayloadBuffer> payload_buffer)
    : pts_(pts),
      pts_rate_(pts_rate),
      keyframe_(keyframe),
      end_of_stream_(end_of_stream),
      payload_buffer_(std::move(payload_buffer)) {}

Packet::~Packet() {}

int64_t Packet::GetPts(media::TimelineRate pts_rate) {
  // We're asking for an inexact product here, because, in some cases,
  // pts_rate / pts_rate_ can't be represented exactly as a TimelineRate.
  // Using this approach produces small errors in the resulting pts in those
  // cases.
  // TODO(dalesat): Do the 128-bit calculation required to do this exactly.
  return (pts_rate == pts_rate_)
             ? pts()
             : (pts() * media::TimelineRate::Product(
                            pts_rate, pts_rate_.Inverse(), false));
}

uint64_t Packet::GetLabel() { return 0; }

void Packet::SetPtsRate(media::TimelineRate pts_rate) {
  if (pts_rate == pts_rate_) {
    return;
  }

  pts_ = GetPts(pts_rate);
  pts_rate_ = pts_rate;
}

}  // namespace media_player
