// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing_header.h"
#include <iomanip>
#include <sstream>

namespace overnet {

static const uint64_t kMaxDestCount = 128;

std::ostream& operator<<(std::ostream& out, NodeId node_id) {
  return out << node_id.ToString();
}

std::ostream& operator<<(std::ostream& out, StreamId stream_id) {
  return out << stream_id.ToString();
}

std::ostream& operator<<(std::ostream& out, SeqNum seq_num) {
  return out << seq_num.ToString();
}

std::string NodeId::ToString() const {
  std::ostringstream tmp;
  tmp << "[";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << ((id_ >> 48) & 0xffff);
  tmp << "_";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << ((id_ >> 32) & 0xffff);
  tmp << "_";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << ((id_ >> 16) & 0xffff);
  tmp << "_";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << (id_ & 0xffff);
  tmp << "]";
  return tmp.str();
}

std::string StreamId::ToString() const {
  std::ostringstream tmp;
  tmp << id_;
  return tmp.str();
}

std::string SeqNum::ToString() const {
  std::ostringstream tmp;
  tmp << std::hex << std::setfill('0') << std::setw(wire_length() * 2)
      << Reconstruct(0);
  return tmp.str();
}

SeqNum::SeqNum(uint64_t seq, uint64_t outstanding_messages) {
  uint8_t width;
  if (outstanding_messages < (1 << 4)) {
    width = 1;
  } else if (outstanding_messages < (1 << 12)) {
    width = 2;
  } else if (outstanding_messages < (1 << 20)) {
    width = 3;
  } else if (outstanding_messages < (1 << 28)) {
    width = 4;
  } else {
    abort();
  }

  switch (width) {
    case 4:
      rep_[3] = (seq >> 22) & 0xff;
    case 3:
      rep_[2] = (seq >> 14) & 0xff;
    case 2:
      rep_[1] = (seq >> 6) & 0xff;
    case 1:
      rep_[0] = ((width - 1) << 6) | (seq & 0x3f);
  }
}

StatusOr<SeqNum> SeqNum::Parse(const uint8_t** bytes, const uint8_t* end) {
  SeqNum r;
  ssize_t rem;
  if (*bytes == end) goto fail;
  r.rep_[0] = *(*bytes)++;
  rem = r.wire_length() - 1;
  if (end - *bytes < rem) goto fail;
  memcpy(r.rep_ + 1, *bytes, rem);
  *bytes += rem;
  return r;

fail:
  return StatusOr<SeqNum>(StatusCode::INVALID_ARGUMENT,
                          "Failed to parse sequence number");
}

uint64_t SeqNum::Reconstruct(uint64_t window_base) const {
  uint8_t width = (rep_[0] >> 6) + 1;
  uint64_t result = window_base;
  switch (width) {
    case 4:
      result &= ~(0xffull << 22);
      result |= static_cast<uint64_t>(rep_[3]) << 22;
    case 3:
      result &= ~(0xffull << 14);
      result |= static_cast<uint64_t>(rep_[2]) << 14;
    case 2:
      result &= ~(0xffull << 6);
      result |= static_cast<uint64_t>(rep_[1]) << 6;
    case 1:
      result &= ~(0x3full);
      result |= rep_[0] & 0x3f;
  }
  return result;
}

uint64_t RoutingHeader::DeriveFlags(NodeId writer, NodeId target) const {
  uint64_t flags = 0;
  if (src_ == writer && dsts_.size() == 1 && dsts_[0].dst() == target) {
    flags |= kFlagIsLocal;
  }
  if (is_control_) flags |= kFlagIsControl;
  const uint8_t robits = static_cast<uint8_t>(reliability_and_ordering_);
  assert(robits < (1 << 3));
  flags |= robits << kFlagsReliabilityAndOrderingShift;
  flags |= dsts_.size() << kFlagsDestinationCountShift;
  return flags;
}

RoutingHeader::Writer::Writer(const RoutingHeader* hdr, NodeId writer,
                              NodeId target)
    : hdr_(hdr),
      flags_value_(hdr->DeriveFlags(writer, target)),
      flags_length_(varint::WireSizeFor(flags_value_)),
      payload_length_length_(varint::WireSizeFor(hdr->payload_length_)),
      wire_length_(0) {
  // build shadow destination structure
  for (const auto& dst : hdr->dsts_) {
    dsts_.emplace_back(Destination{dst.stream_id_.wire_length()});
  }

  // calculate wire size
  wire_length_ += flags_length_;
  if (!IsLocal()) wire_length_ += hdr_->src_.wire_length();
  for (size_t i = 0; i < dsts_.size(); i++) {
    if (!IsLocal()) wire_length_ += hdr_->dsts_[i].dst().wire_length();
    wire_length_ += dsts_[i].stream_len;
    wire_length_ += hdr_->dsts_[i].seq().wire_length();
  }
  wire_length_ += payload_length_length_;
}

uint8_t* RoutingHeader::Writer::Write(uint8_t* bytes) const {
  uint8_t* p = bytes;
  p = varint::Write(flags_value_, flags_length_, p);
  if (!IsLocal()) p = hdr_->src_.Write(p);
  for (size_t i = 0; i < dsts_.size(); i++) {
    if (!IsLocal()) p = hdr_->dsts_[i].dst().Write(p);
    p = hdr_->dsts_[i].stream_id().Write(dsts_[i].stream_len, p);
    p = hdr_->dsts_[i].seq().Write(p);
  }
  p = varint::Write(hdr_->payload_length_, payload_length_length_, p);
  assert(p == bytes + wire_length());
  return p;
}

StatusOr<RoutingHeader> RoutingHeader::Parse(const uint8_t** bytes,
                                             const uint8_t* end, NodeId reader,
                                             NodeId writer) {
  uint64_t flags;
  if (!varint::Read(bytes, end, &flags)) {
    return StatusOr<RoutingHeader>(StatusCode::INVALID_ARGUMENT,
                                   "Failed to parse routing header flags");
  }
  bool is_local = (flags & kFlagIsLocal) != 0;
  bool is_control = (flags & kFlagIsControl) != 0;
  ReliabilityAndOrdering reliability_and_ordering =
      static_cast<ReliabilityAndOrdering>(
          (flags >> kFlagsReliabilityAndOrderingShift) &
          kReliabilityAndOrderingMask);
  bool reserved_ok = (flags & kFlagReservedMask) == 0;
  uint64_t dest_count = flags >> kFlagsDestinationCountShift;
  if (!reserved_ok) {
    return StatusOr<RoutingHeader>(
        StatusCode::INVALID_ARGUMENT,
        "Routing header reserved flag bit set: not sure what to do");
  }
  if (dest_count > kMaxDestCount) {
    return StatusOr<RoutingHeader>(
        StatusCode::INVALID_ARGUMENT,
        "Destination count too high in routing header");
  }
  if (dest_count > 1 && is_local) {
    return StatusOr<RoutingHeader>(
        StatusCode::INVALID_ARGUMENT,
        "Link-local messages cannot be used for multicast");
  }
  uint64_t src;
  if (!is_local) {
    if (!ParseLE64(bytes, end, &src)) {
      return StatusOr<RoutingHeader>(StatusCode::INVALID_ARGUMENT,
                                     "Failed to parse source node");
    }
  } else {
    src = writer.get();
  }
  std::vector<Destination> destinations;
  destinations.reserve(dest_count);
  for (uint64_t i = 0; i < dest_count; i++) {
    uint64_t dst;
    if (!is_local) {
      if (!ParseLE64(bytes, end, &dst)) {
        return StatusOr<RoutingHeader>(StatusCode::INVALID_ARGUMENT,
                                       "Failed to parse destination node");
      }
    } else {
      dst = reader.get();
    }
    uint64_t stream_id;
    if (!varint::Read(bytes, end, &stream_id)) {
      return StatusOr<RoutingHeader>(
          StatusCode::INVALID_ARGUMENT,
          "Failed to parse stream id from routing header");
    }
    auto seq_num = SeqNum::Parse(bytes, end);
    if (seq_num.is_error()) {
      return StatusOr<RoutingHeader>(seq_num.AsStatus());
    }
    destinations.emplace_back(NodeId(dst), StreamId(stream_id), *seq_num.get());
  }
  uint64_t payload_length;
  if (!varint::Read(bytes, end, &payload_length)) {
    return StatusOr<RoutingHeader>(StatusCode::INVALID_ARGUMENT,
                                   "Failed to parse payload length");
  }
  return RoutingHeader(NodeId(src), is_control, reliability_and_ordering,
                       std::move(destinations), payload_length);
}

std::ostream& operator<<(std::ostream& out, const RoutingHeader& h) {
  out << "RoutingHeader{src:" << h.src() << " ctl:" << h.is_control()
      << " ro:" << ReliabilityAndOrderingString(h.reliability_and_ordering())
      << " payload:" << h.payload_length() << " dsts:{";
  int i = 0;
  for (const auto& dst : h.destinations()) {
    if (i) out << " ";
    out << "[" << (i++) << "] dst:" << dst.dst()
        << " stream_id:" << dst.stream_id() << " seq:" << dst.seq();
  }
  return out << "}}";
}

}  // namespace overnet
