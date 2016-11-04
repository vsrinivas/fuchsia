// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/packet.h"

#include "apps/media/src/framework/payload_allocator.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

Packet::Packet(int64_t pts,
               TimelineRate pts_rate,
               bool end_of_stream,
               size_t size,
               void* payload)
    : pts_(pts),
      pts_rate_(pts_rate),
      end_of_stream_(end_of_stream),
      size_(size),
      payload_(payload) {
  FTL_DCHECK((size == 0) == (payload == nullptr));
}

int64_t Packet::GetPts(TimelineRate pts_rate) {
  // We're asking for an inexact product here, because, in some cases,
  // pts_rate / pts_rate_ can't be represented exactly as a TimelineRate.
  // Using this approach produces small errors in the resulting pts in those
  // cases.
  // TODO(dalesat): Do the 128-bit calculation required to do this exactly.
  return (pts_rate == pts_rate_)
             ? pts()
             : (pts() *
                TimelineRate::Product(pts_rate, pts_rate_.Inverse(), false));
}

void Packet::SetPtsRate(TimelineRate pts_rate) {
  if (pts_rate == pts_rate_) {
    return;
  }

  pts_ = GetPts(pts_rate);
  pts_rate_ = pts_rate;
}

class PacketImpl : public Packet {
 public:
  PacketImpl(int64_t pts,
             TimelineRate pts_rate,
             bool end_of_stream,
             size_t size,
             void* payload,
             PayloadAllocator* allocator)
      : Packet(pts, pts_rate, end_of_stream, size, payload),
        allocator_(allocator) {}

 protected:
  ~PacketImpl() override{};

  void Release() override {
    // In the default implementation, payload() will be nullptr if and only if
    // allocator_ is nullptr. Subclasses have the option of having a non-null
    // payload() and handling deallocation themselves, so allocator_ can be
    // nullptr even when payload() is not.
    if (payload() != nullptr && allocator_ != nullptr) {
      allocator_->ReleasePayloadBuffer(payload());
    }
    delete this;
  }

 private:
  PayloadAllocator* allocator_;
};

// static
PacketPtr Packet::Create(int64_t pts,
                         TimelineRate pts_rate,
                         bool end_of_stream,
                         size_t size,
                         void* payload,
                         PayloadAllocator* allocator) {
  FTL_DCHECK(payload == nullptr || allocator != nullptr);
  return PacketPtr(
      new PacketImpl(pts, pts_rate, end_of_stream, size, payload, allocator));
}

// static
PacketPtr Packet::CreateNoAllocator(int64_t pts,
                                    TimelineRate pts_rate,
                                    bool end_of_stream,
                                    size_t size,
                                    void* payload) {
  return PacketPtr(
      new PacketImpl(pts, pts_rate, end_of_stream, size, payload, nullptr));
}

// static
PacketPtr Packet::CreateEndOfStream(int64_t pts, TimelineRate pts_rate) {
  return PacketPtr(new PacketImpl(pts, pts_rate,
                                  true,       // end_of_stream
                                  0,          // size
                                  nullptr,    // payload
                                  nullptr));  // allocator
}

}  // namespace media
}  // namespace mojo
