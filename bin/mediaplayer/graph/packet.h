// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_GRAPH_PACKET_H_
#define GARNET_BIN_MEDIAPLAYER_GRAPH_PACKET_H_

#include <limits>
#include <memory>
#include "garnet/bin/mediaplayer/graph/payloads/payload_allocator.h"
#include "garnet/bin/mediaplayer/graph/types/stream_type.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {

class Packet;

// Shared pointer for packets.
typedef std::shared_ptr<Packet> PacketPtr;

// Stream packet (access unit) possibly bearing a slice of stream content
// (payload).
// TODO(dalesat): Revisit this definition:
// 1) Remove pts_rate().
// 2) Remove end_of_stream().
class Packet {
 public:
  static const int64_t kUnknownPts = std::numeric_limits<int64_t>::min();

  // Creates a packet.
  static PacketPtr Create(int64_t pts, media::TimelineRate pts_rate,
                          bool keyframe, bool end_of_stream,
                          fbl::RefPtr<PayloadBuffer> load_buffer);

  // Creates an end-of-stream packet with no payload.
  static PacketPtr CreateEndOfStream(int64_t pts, media::TimelineRate pts_rate);

  Packet(int64_t pts, media::TimelineRate pts_rate, bool keyframe,
         bool end_of_stream, fbl::RefPtr<PayloadBuffer> load_buffer);

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

  // Returns the size in bytes of the packet payload or 0 if the packet has no
  // payload.
  size_t size() const { return payload_buffer_ ? payload_buffer_->size() : 0; }

  // Returns a pointer to the packet payload or nullptr if there is no payload
  // or the payload isn't mapped into process local memory.
  void* payload() const {
    return payload_buffer_ ? payload_buffer_->data() : nullptr;
  }

  // Returns a raw pointer to the packet's payload buffer.
  PayloadBuffer* payload_buffer() { return payload_buffer_.get(); }

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

 private:
  int64_t pts_;
  media::TimelineRate pts_rate_;
  bool keyframe_;
  bool end_of_stream_;
  fbl::RefPtr<PayloadBuffer> payload_buffer_;
  std::unique_ptr<StreamType> revised_stream_type_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_GRAPH_PACKET_H_
