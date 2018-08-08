// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <tuple>
#include <vector>
#include "node_id.h"
#include "optional.h"
#include "seq_num.h"
#include "slice.h"
#include "status.h"
#include "stream_id.h"
#include "varint.h"

namespace overnet {

// Routing headers are passed over links between nodes in a (potentially)
// non-private way. They should expose a minimal amount of information to route
// a message to the correct destination.
//
// A routing header contains source and (potentially multiple) destination
// information - multiple in the case of multicasting.
//
// Additionally it specifies control vs payload - allowing two channels per
// stream, one being intended only for control information.
//
// Finally it specifies the reliability/ordering data. This is redundant
// information (as it should be known by each node at the endpoint of the stream
// - and indeed should be verified there), but can be used by intermediatories
// to provide better back-pressure behavior.
class RoutableMessage {
 public:
  // A single destination for a message... a triplet of:
  // - dst - the destination node
  // - stream_id - which stream is this message for
  // - seq - the sequence number of this message within its stream
  class Destination {
    friend class RoutableMessage;

   public:
    Destination(NodeId dst, StreamId stream_id, Optional<SeqNum> seq)
        : dst_(dst), stream_id_(stream_id), seq_(seq) {}

    NodeId dst() const { return dst_; }
    StreamId stream_id() const { return stream_id_; }
    const Optional<SeqNum>& seq() const { return seq_; }

    friend bool operator==(const Destination& a, const Destination& b) {
      return std::tie(a.dst_, a.stream_id_, a.seq_) ==
             std::tie(b.dst_, b.stream_id_, b.seq_);
    }

   private:
    NodeId dst_;
    StreamId stream_id_;
    Optional<SeqNum> seq_;
  };

  // Since these objects are potentially expensive to copy, we disable the
  // implicit copy operations to avoid doing so by mistake
  RoutableMessage(const RoutableMessage&) = delete;
  RoutableMessage& operator=(const RoutableMessage&) = delete;

  // Move construction
  RoutableMessage(RoutableMessage&& other)
      : src_(other.src_),
        is_control_(other.is_control_),
        dsts_(std::move(other.dsts_)),
        payload_(std::move(other.payload_)) {}

  // Payload message header constructor
  explicit RoutableMessage(NodeId src, bool is_control, Slice payload)
      : src_(src), is_control_(is_control), payload_(std::move(payload)) {}

  static StatusOr<RoutableMessage> Parse(Slice source, NodeId reader,
                                         NodeId writer);
  Slice Write(NodeId writer, NodeId target) const;

  RoutableMessage& AddDestination(NodeId peer, StreamId stream, SeqNum seq) {
    assert(!is_control());
    dsts_.emplace_back(peer, stream, seq);
    return *this;
  }

  RoutableMessage& AddDestination(NodeId peer, StreamId stream) {
    assert(is_control());
    dsts_.emplace_back(peer, stream, Nothing);
    return *this;
  }

  NodeId src() const { return src_; }
  const Slice& payload() const { return payload_; }
  Slice* mutable_payload() { return &payload_; }
  bool is_control() const { return is_control_; }

  // Return a new RoutableMessage with a different set of destinations (but
  // otherwise equal)
  RoutableMessage WithDestinations(std::vector<Destination> dsts) const {
    return RoutableMessage(src_, is_control_, std::move(dsts),
                           std::move(payload_));
  }

  const std::vector<Destination>& destinations() const { return dsts_; }

  friend bool operator==(const RoutableMessage& a, const RoutableMessage& b) {
    return std::tie(a.src_, a.is_control_, a.payload_, a.dsts_) ==
           std::tie(b.src_, b.is_control_, b.payload_, b.dsts_);
  }

 private:
  RoutableMessage(NodeId src, bool is_control, std::vector<Destination> dsts,
                  Slice payload)
      : src_(src),
        is_control_(is_control),
        dsts_(std::move(dsts)),
        payload_(std::move(payload)) {}

  NodeId src_;
  bool is_control_;
  // TODO(ctiller): small vector optimization
  std::vector<Destination> dsts_;
  Slice payload_;
};

std::ostream& operator<<(std::ostream& out, const RoutableMessage& h);

}  // namespace overnet
