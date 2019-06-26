// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/protocol/ack_frame.h"

#include <assert.h>

#include "src/connectivity/overnet/lib/protocol/varint.h"

namespace overnet {

AckFrame::Writer::Writer(const AckFrame* ack_frame)
    : ack_frame_(ack_frame), wire_length_(ack_frame_->WrittenLength()) {}

uint64_t AckFrame::WrittenLength() const {
  uint64_t wire_length =
      varint::WireSizeFor(ack_to_seq_) + varint::WireSizeFor(DelayAndFlags());
  for (const auto block : blocks_) {
    wire_length += varint::WireSizeFor(block.acks);
    wire_length += varint::WireSizeFor(block.nacks);
  }
  return wire_length;
}

uint8_t* AckFrame::Writer::Write(uint8_t* out) const {
  uint8_t* p = out;
  p = varint::Write(ack_frame_->ack_to_seq_, p);
  p = varint::Write(ack_frame_->DelayAndFlags(), p);
  for (const auto block : ack_frame_->blocks_) {
    p = varint::Write(block.acks, p);
    p = varint::Write(block.nacks, p);
  }
  assert(p == out + wire_length_);
  return p;
}

uint64_t AckFrame::DelayAndFlags() const {
  uint64_t delay_part =
      (ack_delay_us_ >> 63) ? 0xffff'ffff'ffff'fffe : (ack_delay_us_ << 1);
  uint64_t partial_part = partial_ ? 1 : 0;
  return delay_part | partial_part;
}

StatusOr<AckFrame> AckFrame::Parse(Slice slice) {
  const uint8_t* bytes = slice.begin();
  const uint8_t* end = slice.end();
  uint64_t ack_to_seq;
  if (!varint::Read(&bytes, end, &ack_to_seq)) {
    return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                              "Failed to parse ack_to_seq from ack frame");
  }
  if (ack_to_seq == 0) {
    return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                              "Ack frame cannot ack_to_seq 0");
  }
  uint64_t delay_and_flags;
  if (!varint::Read(&bytes, end, &delay_and_flags)) {
    return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                              "Failed to parse delay_and_flags from ack frame");
  }
  bool is_partial = (delay_and_flags & 1) != 0;
  uint64_t ack_delay_us = delay_and_flags >> 1;
  AckFrame frame(ack_to_seq, ack_delay_us);
  frame.partial_ = is_partial;
  uint64_t base = ack_to_seq;
  while (bytes != end) {
    uint64_t acks, nacks;
    if (!varint::Read(&bytes, end, &acks)) {
      return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                                "Failed to read ack count from ack frame");
    }
    if (!varint::Read(&bytes, end, &nacks)) {
      return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                                "Failed to read nack count from ack frame");
    }
    if (acks >= base) {
      return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                                "Failed to read nack (too many acks)");
    }
    if (nacks > base - acks) {
      return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                                "Failed to read nack (too many nacks)");
    }
    if (nacks == 0) {
      return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                                "Nack count cannot be zero");
    }
    base -= acks;
    base -= nacks;
    frame.blocks_.push_back(Block{acks, nacks});
    frame.last_nack_ = base + 1;
  }
  return StatusOr<AckFrame>(std::move(frame));
}

std::ostream& operator<<(std::ostream& out, const AckFrame& ack_frame) {
  out << "ACK{to:" << ack_frame.ack_to_seq()
      << ", delay:" << ack_frame.ack_delay_us()
      << "us, partial:" << (ack_frame.partial() ? "yes" : "no") << ", nack=[";
  for (auto n : ack_frame.nack_seqs()) {
    out << n << ",";
  }
  return out << "]}";
}

}  // namespace overnet
