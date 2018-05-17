// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ack_frame.h"
#include <assert.h>
#include "varint.h"

namespace overnet {

AckFrame::Writer::Writer(const AckFrame* ack_frame)
    : ack_frame_(ack_frame),
      ack_to_seq_length_(varint::WireSizeFor(ack_frame_->ack_to_seq_)),
      ack_delay_us_length_(varint::WireSizeFor(ack_frame_->ack_delay_us_)) {
  wire_length_ = ack_to_seq_length_ + ack_delay_us_length_;
  nack_length_.reserve(ack_frame_->nack_seqs_.size());
  uint64_t base = ack_frame_->ack_to_seq_;
  for (auto n : ack_frame_->nack_seqs_) {
    auto enc = base - n;
    auto l = varint::WireSizeFor(enc);
    wire_length_ += l;
    nack_length_.push_back(l);
    base = n;
  }
}

uint8_t* AckFrame::Writer::Write(uint8_t* out) const {
  uint8_t* p = out;
  p = varint::Write(ack_frame_->ack_to_seq_, ack_to_seq_length_, p);
  p = varint::Write(ack_frame_->ack_delay_us_, ack_delay_us_length_, p);
  uint64_t base = ack_frame_->ack_to_seq_;
  for (size_t i = 0; i < nack_length_.size(); i++) {
    auto n = ack_frame_->nack_seqs_[i];
    auto enc = base - n;
    p = varint::Write(enc, nack_length_[i], p);
    base = n;
  }
  assert(p == out + wire_length_);
  return p;
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
  uint64_t ack_delay_us;
  if (!varint::Read(&bytes, end, &ack_delay_us)) {
    return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                              "Failed to parse ack_delay_us from ack frame");
  }
  AckFrame frame(ack_to_seq, ack_delay_us);
  uint64_t base = ack_to_seq;
  while (bytes != end) {
    uint64_t offset;
    if (!varint::Read(&bytes, end, &offset)) {
      return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                                "Failed to read nack offset from ack frame");
    }
    if (offset >= base) {
      return StatusOr<AckFrame>(StatusCode::INVALID_ARGUMENT,
                                "Failed to read nack");
    }
    const uint64_t seq = base - offset;
    frame.AddNack(seq);
    base = seq;
  }
  return StatusOr<AckFrame>(std::move(frame));
}

std::ostream& operator<<(std::ostream& out, const AckFrame& ack_frame) {
  out << "ACK{to:" << ack_frame.ack_to_seq()
      << ", delay:" << ack_frame.ack_delay_us() << "us, nack=[";
  for (auto n : ack_frame.nack_seqs()) {
    out << n << ",";
  }
  return out << "]}";
}

}  // namespace overnet
