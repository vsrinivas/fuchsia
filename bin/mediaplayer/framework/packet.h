// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_PACKET_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_PACKET_H_

#include <limits>
#include <memory>

#include "garnet/bin/mediaplayer/framework/payload_allocator.h"
#include "garnet/bin/mediaplayer/framework/types/stream_type.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {

class Packet;

// Shared pointer for packets.
typedef std::shared_ptr<Packet> PacketPtr;

// Media packet abstract base class. Subclasses may be defined as needed.
// |Packet::Create|, |Packet::CreateNoAllocator| and |Packet::CreateEndOfStream|
// use an implementation with no special behavior.
// TODO(dalesat): Revisit this definition:
// 1) We probably need an extensible way to add metadata to packets.
// 2) The relationship to the allocator could be clearer.
class Packet {
 public:
  static const int64_t kUnknownPts = std::numeric_limits<int64_t>::min();

  // Creates a packet. If size is 0, payload must be nullptr and vice-versa.
  // If payload is not nullptr, an allocator must be provided.
  static PacketPtr Create(int64_t pts, media::TimelineRate pts_rate,
                          bool keyframe, bool end_of_stream, size_t size,
                          void* payload,
                          std::shared_ptr<PayloadAllocator> allocator);

  // Creates a packet. If size is 0, payload must be nullptr and vice-versa.
  // No allocator is provided, and the payload will not be released when the
  // packet is released.
  static PacketPtr CreateNoAllocator(int64_t pts, media::TimelineRate pts_rate,
                                     bool keyframe, bool end_of_stream,
                                     size_t size, void* payload);

  // Creates an end-of-stream packet with no payload.
  static PacketPtr CreateEndOfStream(int64_t pts, media::TimelineRate pts_rate);

  virtual ~Packet();

  // Returns the presentation timestamp of the packet where the duration of a
  // tick is given by pts_rate().
  int64_t pts() const { return pts_; }

  // Returns the PTS tick rate. pts_rate().subject_delta() is the number of
  // ticks corresponding to pts_rate().reference_delta() seconds. To convert
  // a time value from seconds to PTS ticks, use seconds * pts_rate(). To
  // convert a time value from PTS ticks to seconds, use seconds / pts_rate().
  media::TimelineRate pts_rate() const { return pts_rate_; }

  // Indicates whether this is a keyframe.
  bool keyframe() const { return keyframe_; }

  // Indicates whether this is the last packet in the stream.
  bool end_of_stream() const { return end_of_stream_; }

  // Size in bytes of the packet payload.
  size_t size() const { return size_; }

  // Pointer to the packet payload or nullptr if size() is zero.
  void* payload() const { return payload_; }

  // Retrieves the PTS using the specified PTS tick rate. Use this method to
  // obtain the PTS at a specific tick rate once, possibly at the cost of a
  // TimelineRate::Product call and a TimelineRate::Scale call.
  int64_t GetPts(media::TimelineRate pts_rate);

  // Sets the PTS rate and adjusts PTS accordingly. Use this method to adjust
  // the packet's PTS to a desired PTS tick rate so that future calls to
  // pts() will use the desired rate. This method has approximately the same
  // cost as GetPts, but may save the expense of subsequent conversions.
  void SetPtsRate(media::TimelineRate pts_rate);

  // Gets the revised stream type, which may be null.
  const std::unique_ptr<StreamType>& revised_stream_type() {
    return revised_stream_type_;
  }

  // Sets the revised stream type for the packet.
  void SetRevisedStreamType(std::unique_ptr<StreamType> stream_type) {
    revised_stream_type_ = std::move(stream_type);
  }

  // Returns a numeric label used in instrumentation. The default implementation
  // returns 0. Specialized implementations are free to do otherwise.
  virtual uint64_t GetLabel();

 protected:
  Packet(int64_t pts, media::TimelineRate pts_rate, bool keyframe,
         bool end_of_stream, size_t size, void* payload);

 private:
  int64_t pts_;
  media::TimelineRate pts_rate_;
  bool keyframe_;
  bool end_of_stream_;
  size_t size_;
  void* payload_;
  std::unique_ptr<StreamType> revised_stream_type_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_PACKET_H_
