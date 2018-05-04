// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#include "status.h"
#include "varint.h"

namespace overnet {

inline uint8_t* WriteLE64(uint64_t x, uint8_t* bytes) {
  memcpy(bytes, &x, sizeof(x));
  return bytes + sizeof(x);
}

// Address of a node on the overlay network. This is intended to be relatively
// random and unguessable.
class NodeId {
 public:
  explicit NodeId(uint64_t id) : id_(id) {}
  bool operator==(NodeId other) const { return id_ == other.id_; }
  bool operator!=(NodeId other) const { return id_ != other.id_; }

  uint64_t Hash() const { return id_; }

  size_t wire_length() const { return sizeof(id_); }
  uint8_t* Write(uint8_t* dst) const { return WriteLE64(id_, dst); }

 private:
  uint64_t id_;
};

// Identifier of an active stream of communication between two nodes.
class StreamId {
 public:
  explicit StreamId(uint64_t id) : id_(id) {}
  bool operator==(StreamId other) const { return id_ == other.id_; }
  bool operator!=(StreamId other) const { return id_ != other.id_; }

  uint64_t Hash() const { return id_; }

  uint8_t wire_length() const { return varint::WireSizeFor(id_); }
  uint8_t* Write(uint8_t wire_length, uint8_t* dst) const {
    return varint::Write(id_, wire_length, dst);
  }

 private:
  uint64_t id_;
};

// A sequence number
class SeqNum {
 public:
  // Construct with the sequence number and the number of outstanding messages
  // in the same stream - the wire representation will be scaled such that the
  // correct sequence number is unambiguous.
  SeqNum(uint64_t seq, uint64_t outstanding_messages);

  static bool IsOutstandingMessagesLegal(uint64_t outstanding_messages) {
    return outstanding_messages < (1 << 28);
  }

  size_t wire_length() const { return (rep_[0] >> 6) + 1; }
  uint8_t* Write(uint8_t* dst) const {
    memcpy(dst, rep_, wire_length());
    return dst + wire_length();
  }

  uint64_t Reconstruct(uint64_t window_base) const;

  // Helper to make writing mocks easier.
  uint64_t ReconstructFromZero_TestOnly() const { return Reconstruct(0); }

 private:
  uint8_t rep_[4];
};

// Reliability and ordering mode for a stream
enum class ReliabilityAndOrdering : uint8_t {
  ReliableOrdered = 0,
  UnreliableOrdered = 1,
  ReliableUnordered = 2,
  UnreliableUnordered = 3,
  // The last sent message in a stream is reliable, and sending a message makes
  // all previous messages in the stream unreliable.
  TailReliable = 4,
};

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

  uint64_t DeriveFlags(NodeId writer, NodeId target) const;
};

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