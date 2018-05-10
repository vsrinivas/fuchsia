// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "seq_num.h"
#include <iomanip>
#include <sstream>

namespace overnet {

std::ostream& operator<<(std::ostream& out, SeqNum seq_num) {
  return out << seq_num.ToString();
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

}  // namespace overnet