// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <iosfwd>
#include "status.h"
#include "string.h"

namespace overnet {

// A sequence number
class SeqNum {
 public:
  static constexpr size_t kMaxWireLength = 4;

  // Construct with the sequence number and the number of outstanding messages
  // in the same stream - the wire representation will be scaled such that the
  // correct sequence number is unambiguous.
  SeqNum(uint64_t seq, uint64_t outstanding_messages);

  static StatusOr<SeqNum> Parse(const uint8_t** bytes, const uint8_t* end);

  static bool IsOutstandingMessagesLegal(uint64_t outstanding_messages) {
    return outstanding_messages < (1 << 28);
  }

  size_t wire_length() const { return (rep_[0] >> 6) + 1; }
  uint8_t* Write(uint8_t* dst) const {
    memcpy(dst, rep_, wire_length());
    return dst + wire_length();
  }

  std::string ToString() const;
  uint64_t Reconstruct(uint64_t window_base) const;

  // Helper to make writing mocks easier.
  uint64_t ReconstructFromZero_TestOnly() const { return Reconstruct(0); }

  bool operator==(const SeqNum& rhs) const {
    return wire_length() == rhs.wire_length() &&
           0 == memcmp(rep_, rhs.rep_, wire_length());
  }
  bool operator!=(const SeqNum& rhs) const { return !operator==(rhs); }

 private:
  SeqNum() {}
  uint8_t rep_[kMaxWireLength];
};

std::ostream& operator<<(std::ostream& out, SeqNum seq_num);

}  // namespace overnet
