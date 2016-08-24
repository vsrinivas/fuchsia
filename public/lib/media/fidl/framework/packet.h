// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_PACKET_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_PACKET_H_

#include <limits>
#include <memory>

#include "apps/media/services/framework/payload_allocator.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

class Packet;

// Used for PacketPtr.
struct PacketDeleter {
  void operator()(Packet* ptr) const;
};

// Unique pointer for packets.
typedef std::unique_ptr<Packet, PacketDeleter> PacketPtr;

// Media packet abstract base class. Subclasses may be defined as needed.
// Packet::Create and Packet::CreateEndOfStream use an implementation with
// no special behavior.
// TODO(dalesat): Revisit this definition:
// 1) We probably need an extensible way to add metadata to packets.
// 2) The relationship to the allocator could be clearer.
class Packet {
 public:
  static const int64_t kUnknownPts = std::numeric_limits<int64_t>::min();

  // Creates a packet. If size is 0, payload must be nullptr and vice-versa.
  // If payload is not nullptr, an allocator must be provided.
  static PacketPtr Create(int64_t pts,
                          bool end_of_stream,
                          size_t size,
                          void* payload,
                          PayloadAllocator* allocator);

  // Creates a packet. If size is 0, payload must be nullptr and vice-versa.
  // No allocator is provided, and the payload will not be released when the
  // packet is released.
  static PacketPtr CreateNoAllocator(int64_t pts,
                                     bool end_of_stream,
                                     size_t size,
                                     void* payload);

  // Creates an end-of-stream packet with no payload.
  static PacketPtr CreateEndOfStream(int64_t pts);

  int64_t pts() const { return pts_; }

  bool end_of_stream() const { return end_of_stream_; }

  size_t size() const { return size_; }

  void* payload() const { return payload_; }

 protected:
  Packet(int64_t pts, bool end_of_stream, size_t size, void* payload);

  virtual ~Packet() {}

  virtual void Release() = 0;

 private:
  int64_t pts_;
  bool end_of_stream_;
  size_t size_;
  void* payload_;

  friend PacketDeleter;
};

inline void PacketDeleter::operator()(Packet* ptr) const {
  DCHECK(ptr);
  ptr->Release();
}

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_PACKET_H_
