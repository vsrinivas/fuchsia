// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PACKET_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PACKET_H_

#include <limits>
#include <memory>

#include "lib/media/timeline/timeline_rate.h"
#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer/graph/payloads/payload_allocator.h"
#include "src/media/playback/mediaplayer/graph/types/stream_type.h"

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
  static const int64_t kNoPts = std::numeric_limits<int64_t>::max();
  static const int64_t kMinPts = std::numeric_limits<int64_t>::min();
  static const int64_t kMaxPts = std::numeric_limits<int64_t>::max() - 1;

  // Creates a packet.
  static PacketPtr Create(int64_t pts, media::TimelineRate pts_rate,
                          bool keyframe, bool discontinuity, bool end_of_stream,
                          size_t size,
                          fbl::RefPtr<PayloadBuffer> payload_buffer);

  // Creates a packet.
  static PacketPtr Create(int64_t pts, media::TimelineRate pts_rate,
                          bool keyframe, bool end_of_stream, size_t size,
                          fbl::RefPtr<PayloadBuffer> payload_buffer);

  // Creates an end-of-stream packet with no payload.
  static PacketPtr CreateEndOfStream(int64_t pts, media::TimelineRate pts_rate);

  // Function type used for |AfterRecycling|.
  using Action = ::fit::function<void(Packet*)>;

  Packet(int64_t pts, media::TimelineRate pts_rate, bool keyframe,
         bool discontinuity, bool end_of_stream, size_t size,
         fbl::RefPtr<PayloadBuffer> payload_buffer);

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

  // Indicates whether this packet follows a discontinuity in the stream.
  bool discontinuity() const { return discontinuity_; }

  // Returns the size in bytes of the packet payload or 0 if the packet has no
  // payload.
  size_t size() const { return size_; }

  // Returns a pointer to the packet payload or nullptr if there is no payload
  // or the payload isn't mapped into process local memory.
  void* payload() const {
    return payload_buffer_ ? payload_buffer_->data() : nullptr;
  }

  // Returns the packet's payload buffer.
  fbl::RefPtr<PayloadBuffer> payload_buffer() const { return payload_buffer_; }

  // Retrieves the PTS using the specified PTS tick rate. Use this method to
  // obtain the PTS at a specific tick rate once, possibly at the cost of a
  // TimelineRate::Product call and a TimelineRate::Scale call.
  int64_t GetPts(media::TimelineRate pts_rate);

  // Sets the PTS value on the packet.
  void SetPts(int64_t pts) { pts_ = pts; }

  // Sets the PTS rate and adjusts PTS accordingly. Use this method to adjust
  // the packet's PTS to a desired PTS tick rate so that future calls to
  // pts() will use the desired rate. This method has approximately the same
  // cost as GetPts, but may save the expense of subsequent conversions.
  void SetPtsRate(media::TimelineRate pts_rate);

  // Gets the revised stream type, which may be null.
  const StreamType* revised_stream_type() const {
    return revised_stream_type_.get();
  }

  // Sets the revised stream type for the packet.
  void SetRevisedStreamType(std::unique_ptr<StreamType> stream_type) {
    revised_stream_type_ = std::move(stream_type);
  }

  // Returns a numeric label used in instrumentation. The default implementation
  // returns 0. Specialized implementations are free to do otherwise.
  virtual uint64_t GetLabel();

  // Registers a function to be called after recycling. This method may only
  // be called once on a given instance. An |Action| should not hold a reference
  // to the |Packet|, because this would produce a circular reference, and the
  // |Packet| would never be released. |action| will be called on an arbitrary
  // thread.
  // NOTE: This method may seem to be oddly named, but the name is consistent
  // with a method in |PayloadBuffer|. Also, |Packet| will soon be managed with
  // a |RefPtr|, which will make the name more relevant.
  // TODO(dalesat): Switch from |std::shared_ptr| to |fbl::RefPtr|.
  void AfterRecycling(Action action);

 private:
  int64_t pts_;
  media::TimelineRate pts_rate_;
  bool keyframe_;
  bool discontinuity_;
  bool end_of_stream_;
  size_t size_;
  fbl::RefPtr<PayloadBuffer> payload_buffer_;
  std::unique_ptr<StreamType> revised_stream_type_;
  Action after_recycling_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_GRAPH_PACKET_H_
