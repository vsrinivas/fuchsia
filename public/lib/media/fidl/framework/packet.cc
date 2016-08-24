// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/framework/packet.h"

#include "apps/media/services/framework/payload_allocator.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

Packet::Packet(int64_t pts, bool end_of_stream, size_t size, void* payload)
    : pts_(pts), end_of_stream_(end_of_stream), size_(size), payload_(payload) {
  DCHECK((size == 0) == (payload == nullptr));
}

class PacketImpl : public Packet {
 public:
  PacketImpl(int64_t pts,
             bool end_of_stream,
             size_t size,
             void* payload,
             PayloadAllocator* allocator)
      : Packet(pts, end_of_stream, size, payload), allocator_(allocator) {}

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
                         bool end_of_stream,
                         size_t size,
                         void* payload,
                         PayloadAllocator* allocator) {
  DCHECK(payload == nullptr || allocator != nullptr);
  return PacketPtr(
      new PacketImpl(pts, end_of_stream, size, payload, allocator));
}

// static
PacketPtr Packet::CreateNoAllocator(int64_t pts,
                                    bool end_of_stream,
                                    size_t size,
                                    void* payload) {
  return PacketPtr(new PacketImpl(pts, end_of_stream, size, payload, nullptr));
}

// static
PacketPtr Packet::CreateEndOfStream(int64_t pts) {
  return PacketPtr(new PacketImpl(pts,
                                  true,       // end_of_stream
                                  0,          // size
                                  nullptr,    // payload
                                  nullptr));  // allocator
}

}  // namespace media
}  // namespace mojo
