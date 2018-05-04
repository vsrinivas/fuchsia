// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing_header.h"

namespace overnet {

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
  // Flags format:
  // bit 0:      is_local -- is this a single destination message whos src is
  //                         this node and whos dst is the peer we're sending
  //                         to?
  // bit 1:      channel - 0 -> control channel, 1 -> payload channel
  // bits 2,3,4: reliability/ordering mode (must be 0 for control channel)
  // bits 5:     reserved (must be zero)
  // bit 6...:   destination count
  uint64_t flags = 0;
  if (src_ == writer && dsts_.size() == 1 && dsts_[0].dst() == target) {
    flags |= 1 << 0;
  }
  if (!is_control_) flags |= 1 << 1;
  const uint8_t robits = static_cast<uint8_t>(reliability_and_ordering_);
  assert(robits < (1 << 3));
  flags |= robits << 2;
  flags |= dsts_.size() << 6;
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

}  // namespace overnet
