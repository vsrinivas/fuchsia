// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <tuple>
#include <vector>
#include "node_id.h"
#include "reliability_and_ordering.h"
#include "seq_num.h"
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
class RoutingHeader {
 public:
  // A single destination for a message... a triplet of:
  // - dst - the destination node
  // - stream_id - which stream is this message for
  // - seq - the sequence number of this message within its stream
  class Destination {
    friend class RoutingHeader;

   public:
    Destination(NodeId dst, StreamId stream_id, SeqNum seq)
        : dst_(dst), stream_id_(stream_id), seq_(seq) {}

    NodeId dst() const { return dst_; }
    StreamId stream_id() const { return stream_id_; }
    SeqNum seq() const { return seq_; }

    friend bool operator==(const Destination& a, const Destination& b) {
      return std::tie(a.dst_, a.stream_id_, a.seq_) ==
             std::tie(b.dst_, b.stream_id_, b.seq_);
    }

   private:
    NodeId dst_;
    StreamId stream_id_;
    SeqNum seq_;
  };

  // Since this object is complicated to write, we do it in two steps:
  // - a Writer object is constructed, which can be used to measure the length
  //   of the eventually written bytes
  // - the Writer object is used to generate the bytes for the wire
  // In doing so we give the caller the flexibility to *not write* should the
  // measured length be too long.
  class Writer {
   public:
    explicit Writer(const RoutingHeader* hdr, NodeId writer, NodeId target);
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    size_t wire_length() const { return wire_length_; }
    uint8_t* Write(uint8_t* bytes) const;

   private:
    bool IsLocal() const { return (flags_value_ & 1) != 0; }

    const RoutingHeader* const hdr_;
    const uint64_t flags_value_;
    const uint8_t flags_length_;
    const uint8_t payload_length_length_;
    struct Destination {
      uint8_t stream_len;
    };
    // TODO(ctiller): the expected length of this vector is one, avoid this
    // allocation
    std::vector<Destination> dsts_;
    size_t wire_length_;
  };

  // Since these objects are potentially expensive to copy, we disable the
  // implicit copy operations to avoid doing so by mistake
  RoutingHeader(const RoutingHeader&) = delete;
  RoutingHeader& operator=(const RoutingHeader&) = delete;

  // Move construction
  RoutingHeader(RoutingHeader&& other)
      : src_(other.src_),
        is_control_(other.is_control_),
        reliability_and_ordering_(other.reliability_and_ordering_),
        dsts_(std::move(other.dsts_)),
        payload_length_(other.payload_length_) {}

  // Payload message header constructor
  explicit RoutingHeader(NodeId src, uint64_t payload_length,
                         ReliabilityAndOrdering reliability_and_ordering)
      : src_(src),
        is_control_(false),
        reliability_and_ordering_(reliability_and_ordering),
        payload_length_(payload_length) {}

  // Control message header constructor
  enum ControlMessage { CONTROL_MESSAGE };
  explicit RoutingHeader(NodeId src, uint64_t payload_length, ControlMessage)
      : src_(src),
        is_control_(true),
        reliability_and_ordering_(ReliabilityAndOrdering::ReliableOrdered),
        payload_length_(payload_length) {}

  static StatusOr<RoutingHeader> Parse(const uint8_t** bytes,
                                       const uint8_t* end, NodeId reader,
                                       NodeId writer);

  RoutingHeader& AddDestination(NodeId peer, StreamId stream, SeqNum seq) {
    dsts_.emplace_back(peer, stream, seq);
    return *this;
  }

  NodeId src() const { return src_; }
  uint64_t payload_length() const { return payload_length_; }
  bool is_control() const { return is_control_; }
  ReliabilityAndOrdering reliability_and_ordering() const {
    return reliability_and_ordering_;
  }

  // Return a new RoutingHeader with a different set of destinations (but
  // otherwise equal)
  RoutingHeader WithDestinations(std::vector<Destination> dsts) const {
    return RoutingHeader(src_, is_control_, reliability_and_ordering_,
                         std::move(dsts), payload_length_);
  }

  const std::vector<Destination>& destinations() const { return dsts_; }

  friend bool operator==(const RoutingHeader& a, const RoutingHeader& b) {
    return std::tie(a.src_, a.is_control_, a.reliability_and_ordering_,
                    a.payload_length_, a.dsts_) ==
           std::tie(b.src_, b.is_control_, b.reliability_and_ordering_,
                    b.payload_length_, b.dsts_);
  }

 private:
  RoutingHeader(NodeId src, bool is_control,
                ReliabilityAndOrdering reliability_and_ordering,
                std::vector<Destination> dsts, uint64_t payload_length)
      : src_(src),
        is_control_(is_control),
        reliability_and_ordering_(reliability_and_ordering),
        dsts_(std::move(dsts)),
        payload_length_(payload_length) {}

  NodeId src_;
  bool is_control_;
  ReliabilityAndOrdering reliability_and_ordering_;
  // TODO(ctiller): small vector optimization
  std::vector<Destination> dsts_;
  uint64_t payload_length_ = 0;

  // Flags format:
  // bit 0:      is_local -- is this a single destination message whos src is
  //                         this node and whos dst is the peer we're sending
  //                         to?
  // bit 1:      channel - 1 -> control channel, 0 -> payload channel
  // bits 2,3,4: reliability/ordering mode (must be 0 for control channel)
  // bits 5:     reserved (must be zero)
  // bit 6...:   destination count
  static constexpr uint64_t kFlagIsLocal = 1;
  static constexpr uint64_t kFlagIsControl = 2;
  static constexpr uint64_t kFlagReservedMask = 32;
  static constexpr uint64_t kFlagsReliabilityAndOrderingShift = 2;
  static constexpr uint64_t kFlagsDestinationCountShift = 6;
  // all reliability and orderings must fit within this mask
  static constexpr uint64_t kReliabilityAndOrderingMask = 0x07;

  uint64_t DeriveFlags(NodeId writer, NodeId target) const;
};

std::ostream& operator<<(std::ostream& out, const RoutingHeader& h);

}  // namespace overnet

namespace std {
template <>
struct hash<overnet::NodeId> {
  size_t operator()(overnet::NodeId id) const { return id.Hash(); }
};
template <>
struct hash<overnet::StreamId> {
  size_t operator()(overnet::StreamId id) const { return id.Hash(); }
};
}  // namespace std