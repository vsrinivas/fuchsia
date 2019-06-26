// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <tuple>
#include <vector>

#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/protocol/varint.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"
#include "src/connectivity/overnet/lib/vocabulary/status.h"

namespace overnet {

class AckFrame {
  struct Block {
    uint64_t acks;
    uint64_t nacks;
    bool operator==(const Block& other) const {
      return acks == other.acks && nacks == other.nacks;
    }
  };
  using Brit = std::vector<Block>::const_reverse_iterator;

 public:
  class Writer {
   public:
    explicit Writer(const AckFrame* ack_frame);

    size_t wire_length() const { return wire_length_; }
    uint8_t* Write(uint8_t* out) const;

   private:
    const AckFrame* const ack_frame_;
    const size_t wire_length_;
  };

  AckFrame(uint64_t ack_to_seq, uint64_t ack_delay_us)
      : partial_(false), ack_to_seq_(ack_to_seq), ack_delay_us_(ack_delay_us) {
    assert(ack_to_seq_ > 0);
  }

  AckFrame(uint64_t ack_to_seq, uint64_t ack_delay_us,
           std::initializer_list<uint64_t> nack_seqs)
      : partial_(false), ack_to_seq_(ack_to_seq), ack_delay_us_(ack_delay_us) {
    assert(ack_to_seq_ > 0);
    for (auto n : nack_seqs) {
      AddNack(n);
    }
  }

  AckFrame(const AckFrame&) = delete;
  AckFrame& operator=(const AckFrame&) = delete;

  AckFrame(AckFrame&& other)
      : partial_(other.partial_),
        ack_to_seq_(other.ack_to_seq_),
        ack_delay_us_(other.ack_delay_us_),
        blocks_(std::move(other.blocks_)),
        last_nack_(other.last_nack_) {}

  AckFrame& operator=(AckFrame&& other) {
    partial_ = other.partial_;
    ack_to_seq_ = other.ack_to_seq_;
    ack_delay_us_ = other.ack_delay_us_;
    blocks_ = std::move(other.blocks_);
    last_nack_ = other.last_nack_;
    return *this;
  }

  void AddNack(uint64_t seq) {
    assert(ack_to_seq_ > 0);
    assert(seq <= ack_to_seq_);
    assert(seq > 0);
    if (!blocks_.empty()) {
      assert(seq < last_nack_);
      if (seq == last_nack_ - 1) {
        blocks_.back().nacks++;
      } else {
        blocks_.emplace_back(Block{last_nack_ - seq - 1, 1});
      }
    } else {
      blocks_.emplace_back(Block{ack_to_seq_ - seq, 1});
    }
    last_nack_ = seq;
  }

  static StatusOr<AckFrame> Parse(Slice slice);

  friend bool operator==(const AckFrame& a, const AckFrame& b) {
    if (std::tie(a.ack_to_seq_, a.ack_delay_us_, a.blocks_, a.partial_) !=
        std::tie(b.ack_to_seq_, b.ack_delay_us_, b.blocks_, b.partial_)) {
      return false;
    }
    if (!a.blocks_.empty()) {
      return a.last_nack_ == b.last_nack_;
    }
    return true;
  }

  uint64_t ack_to_seq() const { return ack_to_seq_; }
  uint64_t ack_delay_us() const { return ack_delay_us_; }
  bool partial() const { return partial_; }

  class NackSeqs {
   public:
    NackSeqs(const AckFrame* ack_frame) : ack_frame_(ack_frame) {}

    class Iterator {
     public:
      Iterator(Brit brit, uint64_t base) : brit_(brit), base_(base) {}

      bool operator!=(const Iterator& other) const {
        return brit_ != other.brit_ || base_ != other.base_ ||
               nack_ != other.nack_;
      }

      void operator++() {
        nack_++;
        if (nack_ == brit_->nacks) {
          base_ += brit_->nacks + brit_->acks;
          ++brit_;
          nack_ = 0;
        }
      }

      uint64_t operator*() const { return base_ + nack_; }

     private:
      Brit brit_;
      uint64_t base_;
      uint64_t nack_ = 0;
    };

    std::vector<uint64_t> AsVector() const {
      std::vector<uint64_t> out;
      for (auto n : *this) {
        out.push_back(n);
      }
      return out;
    }

    Iterator begin() const {
      if (ack_frame_->blocks_.empty()) {
        return end();
      }
      return Iterator(ack_frame_->blocks_.rbegin(), ack_frame_->last_nack_);
    }
    Iterator end() const {
      return Iterator(ack_frame_->blocks_.rend(), ack_frame_->ack_to_seq_ + 1);
    }

   private:
    const AckFrame* ack_frame_;
  };
  NackSeqs nack_seqs() const { return NackSeqs(this); }

  // Move ack_to_seq back in time such that the total ack frame will fit
  // within mss. DelayFn is a function uint64_t -> TimeDelta that returns the
  // ack delay (in microseconds) for a given sequence number.
  template <class DelayFn>
  void AdjustForMSS(uint32_t mss, DelayFn delay_fn) {
    while (!blocks_.empty() && WrittenLength() > mss) {
      partial_ = true;
      auto& block0_acks = blocks_[0].acks;
      auto& block0_nacks = blocks_[0].nacks;
      if (block0_acks > 0) {
        auto new_acks = varint::SmallerRecordedNumber(block0_acks);
        OVERNET_TRACE(DEBUG) << "Trim too long ack (" << WrittenLength()
                             << " > " << mss << " by moving ack " << ack_to_seq_
                             << " to shorter first ack block length "
                             << (ack_to_seq_ - block0_acks + new_acks);
        ack_to_seq_ -= (block0_acks - new_acks);
        block0_acks = new_acks;
      } else {
        assert(block0_nacks > 0);
        auto new_nacks = varint::SmallerRecordedNumber(block0_nacks);
        if (new_nacks == 0) {
          OVERNET_TRACE(DEBUG)
              << "Trim too long ack (" << WrittenLength() << " > " << mss
              << " by eliminating first block and moving first ack to "
              << (ack_to_seq_ - block0_nacks + new_nacks);
          ack_to_seq_ -= (block0_nacks - new_nacks);
          blocks_.erase(blocks_.begin());
        } else {
          OVERNET_TRACE(DEBUG)
              << "Trim too long ack (" << WrittenLength() << " > " << mss
              << " by moving ack " << ack_to_seq_
              << " to shorter first nack block length "
              << (ack_to_seq_ - block0_nacks + new_nacks);
          ack_to_seq_ -= (block0_nacks - new_nacks);
          block0_nacks = new_nacks;
        }
      }
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
  // From ack_to_seq working back we record blocks. A block contains some number
  // of acks followed by some number of nacks.
  std::vector<Block> blocks_;
  uint64_t last_nack_;
};

std::ostream& operator<<(std::ostream& out, const AckFrame& ack_frame);

}  // namespace overnet
