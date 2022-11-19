// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/leb.h"

namespace zxdb {

void AppendULeb(uint64_t value, std::vector<uint8_t>* out) {
  while (true) {
    // Take off the low 7 bits.
    uint8_t cur_bits = static_cast<uint8_t>(value & 0x7f);
    value = value >> 7;

    if (!value) {
      //  No more bits to write, leave the high bit 0 to indicate end of sequence.
      out->push_back(cur_bits);
      return;
    }
    // More bits left to write, set the high bit.
    out->push_back(cur_bits | 0x80);
  }
}

}  // namespace zxdb
