// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_PACKET_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_PACKET_H_

#include <limits>
#include <memory>

#include "apps/media/cpp/timeline_rate.h"
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
                          TimelineRate pts_rate,
                          bool end_of_stream,
                          size_t size,
                          void* payload,
                          PayloadAllocator* allocator);

  // Creates a packet. If size is 0, payload must be nullptr and vice-versa.
  // No allocator is provided, and the payload will not be released when the
  // packet is released.
  static PacketPtr CreateNoAllocator(int64_t pts,
                                     TimelineRate pts_rate,
                                     bool end_of_stream,
                                     size_t size,
                                     void* payload);

  // Creates an end-of-stream packet with no payload.
  static PacketPtr CreateEndOfStream(int64_t pts, TimelineRate pts_rate);

  // Returns the presentation timestamp of the packet where the duration of a
  // tick is given by pts_rate().
  int64_t pts() const { return pts_; }

  // Returns the PTS tick rate. pts_rate().subject_delta() is the number of
  // ticks corresponding to pts_rate().reference_delta() seconds. To convert
  // a time value from seconds to PTS ticks, use seconds * pts_rate(). To
  // convert a time value from PTS ticks to seconds, use seconds / pts_rate().
  TimelineRate pts_rate() const { return pts_rate_; }

  // Indicates whether this is the last packet in the stream.
  bool end_of_stream() const { return end_of_stream_; }

  // Size in bytes of the packet payload.
  size_t size() const { return size_; }

  // Pointer to the packet payload or nullptr if size() is zero.
  void* payload() const { return payload_; }

  // Retrieves the PTS using the specified PTS tick rate. Use this method to
  // obtain the PTS at a specific tick rate once, possibly at the cost of a
  // TimelineRate::Product call and a TimelineRate::Scale call.
  int64_t GetPts(TimelineRate pts_rate);

  // Sets the PTS rate and adjusts PTS accordingly. Use this method to adjust
  // the packet's PTS to a desired PTS tick rate so that future calls to
  // pts() will use the desired rate. This method has approximately the same
  // cost as GetPts, but may save the expense of subsequent conversions.
  void SetPtsRate(TimelineRate pts_rate);

 protected:
  Packet(int64_t pts,
         TimelineRate pts_rate,
         bool end_of_stream,
         size_t size,
         void* payload);

  virtual ~Packet() {}

  virtual void Release() = 0;

 private:
  int64_t pts_;
  TimelineRate pts_rate_;
  bool end_of_stream_;
  size_t size_;
  void* payload_;

  friend PacketDeleter;
};

inline void PacketDeleter::operator()(Packet* ptr) const {
  FTL_DCHECK(ptr);
  ptr->Release();
}

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_PACKET_H_
