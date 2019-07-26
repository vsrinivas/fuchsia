// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <tuple>
#include <vector>

#include "src/connectivity/overnet/deprecated/lib/labels/node_id.h"
#include "src/connectivity/overnet/deprecated/lib/labels/seq_num.h"
#include "src/connectivity/overnet/deprecated/lib/labels/stream_id.h"
#include "src/connectivity/overnet/deprecated/lib/protocol/varint.h"
#include "src/connectivity/overnet/deprecated/lib/vocabulary/optional.h"
#include "src/connectivity/overnet/deprecated/lib/vocabulary/slice.h"
#include "src/connectivity/overnet/deprecated/lib/vocabulary/status.h"

namespace overnet {

struct MessageWithPayload;

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
    Destination(NodeId dst, StreamId stream_id, SeqNum seq)
        : dst_(dst), stream_id_(stream_id), seq_(seq) {}

    NodeId dst() const { return dst_; }
    StreamId stream_id() const { return stream_id_; }
    SeqNum seq() const { return seq_; }

    friend bool operator==(const Destination& a, const Destination& b) {
      return std::tie(a.dst_, a.stream_id_, a.seq_) == std::tie(b.dst_, b.stream_id_, b.seq_);
    }

   private:
    NodeId dst_;
    StreamId stream_id_;
    SeqNum seq_;
  };

  // Since these objects are potentially expensive to copy, we disable the
  // implicit copy operations to avoid doing so by mistake
  RoutableMessage(const RoutableMessage&) = delete;
  RoutableMessage& operator=(const RoutableMessage&) = delete;

  // Move construction
  RoutableMessage(RoutableMessage&& other) : src_(other.src_), dsts_(std::move(other.dsts_)) {}

  // Payload message header constructor
  explicit RoutableMessage(NodeId src) : src_(src) {}

  static StatusOr<MessageWithPayload> Parse(Slice source, NodeId reader, NodeId writer);
  Slice Write(NodeId writer, NodeId target, Slice payload) const;

  RoutableMessage& AddDestination(NodeId peer, StreamId stream, SeqNum seq) {
    dsts_.emplace_back(peer, stream, seq);
    return *this;
  }

  Optional<size_t> MaxPayloadLength(NodeId writer, NodeId target, size_t remaining_space) const;
  size_t MaxHeaderLength() { return HeaderLength(NodeId(0), NodeId(0), nullptr); }

  NodeId src() const { return src_; }

  // Return a new RoutableMessage with a different set of destinations (but
  // otherwise equal)
  RoutableMessage WithDestinations(std::vector<Destination> dsts) const {
    return RoutableMessage(src_, std::move(dsts));
  }

  const std::vector<Destination>& destinations() const { return dsts_; }

  friend bool operator==(const RoutableMessage& a, const RoutableMessage& b) {
    return std::tie(a.src_, a.dsts_) == std::tie(b.src_, b.dsts_);
  }

 private:
  RoutableMessage(NodeId src, std::vector<Destination> dsts) : src_(src), dsts_(std::move(dsts)) {}

  NodeId src_;
  // TODO(ctiller): small vector optimization
  std::vector<Destination> dsts_;

  struct HeaderInfo {
    // TODO(ctiller): small vector optimization
    std::vector<uint8_t> stream_id_len;
    uint64_t flags;
    uint8_t flags_length;
    bool is_local;
  };

  size_t HeaderLength(NodeId writer, NodeId target, HeaderInfo* hinf) const;
};

struct MessageWithPayload {
  MessageWithPayload(RoutableMessage message, Slice payload)
      : message(std::move(message)), payload(std::move(payload)) {}
  RoutableMessage message;
  Slice payload;
};

std::ostream& operator<<(std::ostream& out, const RoutableMessage& h);

}  // namespace overnet
