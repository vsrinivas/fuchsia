// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/packet.h"

#include "garnet/bin/mediaplayer/framework/payload_allocator.h"
#include "lib/fxl/logging.h"

namespace media_player {

Packet::Packet(int64_t pts, media::TimelineRate pts_rate, bool keyframe,
               bool end_of_stream, size_t size, void* payload)
    : pts_(pts),
      pts_rate_(pts_rate),
      keyframe_(keyframe),
      end_of_stream_(end_of_stream),
      size_(size),
      payload_(payload) {
  FXL_DCHECK((size == 0) == (payload == nullptr));
}

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

class PacketImpl : public Packet {
 public:
  PacketImpl(int64_t pts, media::TimelineRate pts_rate, bool keyframe,
             bool end_of_stream, size_t size, void* payload,
             std::shared_ptr<PayloadAllocator> allocator)
      : Packet(pts, pts_rate, keyframe, end_of_stream, size, payload),
        allocator_(allocator) {}

  ~PacketImpl() override {
    // In the default implementation, payload() will be nullptr if and only if
    // allocator_ is nullptr.
    if (payload()) {
      FXL_DCHECK(allocator_);
      allocator_->ReleasePayloadBuffer(payload());
    }
  };

 private:
  std::shared_ptr<PayloadAllocator> allocator_;
};

// static
PacketPtr Packet::Create(int64_t pts, media::TimelineRate pts_rate,
                         bool keyframe, bool end_of_stream, size_t size,
                         void* payload,
                         std::shared_ptr<PayloadAllocator> allocator) {
  FXL_DCHECK(payload == nullptr || allocator != nullptr);
  return std::make_shared<PacketImpl>(pts, pts_rate, keyframe, end_of_stream,
                                      size, payload, allocator);
}

// static
PacketPtr Packet::CreateNoAllocator(int64_t pts, media::TimelineRate pts_rate,
                                    bool keyframe, bool end_of_stream,
                                    size_t size, void* payload) {
  return std::make_shared<PacketImpl>(pts, pts_rate, keyframe, end_of_stream,
                                      size, payload, nullptr);
}

// static
PacketPtr Packet::CreateEndOfStream(int64_t pts, media::TimelineRate pts_rate) {
  return std::make_shared<PacketImpl>(pts, pts_rate,
                                      false,     // keyframe
                                      true,      // end_of_stream
                                      0,         // size
                                      nullptr,   // payload
                                      nullptr);  // allocator
}

}  // namespace media_player
