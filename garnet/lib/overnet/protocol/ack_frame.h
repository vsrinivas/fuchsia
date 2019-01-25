// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <tuple>
#include <vector>
#include "garnet/lib/overnet/environment/trace.h"
#include "garnet/lib/overnet/vocabulary/slice.h"
#include "garnet/lib/overnet/vocabulary/status.h"

namespace overnet {

class AckFrame {
 public:
  class Writer {
   public:
    explicit Writer(const AckFrame* ack_frame);

    size_t wire_length() const { return wire_length_; }
    uint8_t* Write(uint8_t* out) const;

   private:
    const AckFrame* const ack_frame_;
    const uint8_t ack_to_seq_length_;
    const uint8_t delay_and_flags_length_;
    std::vector<uint8_t> nack_length_;
    size_t wire_length_;
  };

  AckFrame(uint64_t ack_to_seq, uint64_t ack_delay_us)
      : ack_to_seq_(ack_to_seq), ack_delay_us_(ack_delay_us) {
    assert(ack_to_seq_ > 0);
  }

  AckFrame(uint64_t ack_to_seq, uint64_t ack_delay_us,
           std::initializer_list<uint64_t> nack_seqs)
      : ack_to_seq_(ack_to_seq), ack_delay_us_(ack_delay_us) {
    assert(ack_to_seq_ > 0);
    for (auto n : nack_seqs)
      AddNack(n);
  }

  AckFrame(const AckFrame&) = delete;
  AckFrame& operator=(const AckFrame&) = delete;

  AckFrame(AckFrame&& other)
      : ack_to_seq_(other.ack_to_seq_),
        ack_delay_us_(other.ack_delay_us_),
        nack_seqs_(std::move(other.nack_seqs_)) {}

  AckFrame& operator=(AckFrame&& other) {
    ack_to_seq_ = other.ack_to_seq_;
    ack_delay_us_ = other.ack_delay_us_;
    nack_seqs_ = std::move(other.nack_seqs_);
    return *this;
  }

  void AddNack(uint64_t seq) {
    assert(ack_to_seq_ > 0);
    assert(seq <= ack_to_seq_);
    if (!nack_seqs_.empty()) {
      assert(seq < nack_seqs_.back());
    }
    nack_seqs_.push_back(seq);
  }

  static StatusOr<AckFrame> Parse(Slice slice);

  friend bool operator==(const AckFrame& a, const AckFrame& b) {
    return std::tie(a.ack_to_seq_, a.ack_delay_us_, a.nack_seqs_) ==
           std::tie(b.ack_to_seq_, b.ack_delay_us_, b.nack_seqs_);
  }

  uint64_t ack_to_seq() const { return ack_to_seq_; }
  uint64_t ack_delay_us() const { return ack_delay_us_; }
  bool partial() const { return partial_; }
  const std::vector<uint64_t>& nack_seqs() const { return nack_seqs_; }

  // Move ack_to_seq back in time such that the total ack frame will fit
  // within mss. DelayFn is a function uint64_t -> TimeDelta that returns the
  // ack delay (in microseconds) for a given sequence number.
  template <class DelayFn>
  void AdjustForMSS(uint32_t mss, DelayFn delay_fn) {
    while (!nack_seqs_.empty() && WrittenLength() > mss) {
      partial_ = true;
      if (ack_to_seq_ != nack_seqs_[0]) {
        OVERNET_TRACE(DEBUG) << "Trim too long ack (" << WrittenLength()
                             << " > " << mss << " by moving ack " << ack_to_seq_
                             << " to first nack " << nack_seqs_[0];
        ack_to_seq_ = nack_seqs_[0];
      } else {
        OVERNET_TRACE(DEBUG)
            << "Trim too long ack (" << WrittenLength() << " > " << mss
            << " by trimming first nack " << nack_seqs_[0];
        nack_seqs_.erase(nack_seqs_.begin());
        ack_to_seq_--;
      }
      ack_delay_us_ = delay_fn(ack_to_seq_);
    }
  }

 private:
  uint64_t DelayAndFlags() const;
  uint64_t WrittenLength() const;

  // Flag indicating that this ack is only a partial acknowledgement, and
  // there's more to come.
  bool partial_ = false;
  // All messages with sequence number prior to ack_to_seq_ are implicitly
  // acknowledged.
  uint64_t ack_to_seq_;
  // How long between receiving ack_delay_seq_ and generating this data
  // structure.
  uint64_t ack_delay_us_;
  // All messages contained in nack_seqs_ need to be resent.
  // NOTE: it's assumed that nack_seqs_ is in-order descending and all value are
  // less than or equal to ack_to_seq_.
  std::vector<uint64_t> nack_seqs_;
};

std::ostream& operator<<(std::ostream& out, const AckFrame& ack_frame);

}  // namespace overnet
