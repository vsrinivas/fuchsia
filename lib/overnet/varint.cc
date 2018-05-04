// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "varint.h"
#include <assert.h>

namespace overnet {
namespace varint {

uint8_t WireSizeFor(uint64_t x) {
  if (x < (1ull << 7)) return 1;
  if (x < (1ull << 14)) return 2;
  if (x < (1ull << 21)) return 3;
  if (x < (1ull << 28)) return 4;
  if (x < (1ull << 35)) return 5;
  if (x < (1ull << 42)) return 6;
  if (x < (1ull << 49)) return 7;
  if (x < (1ull << 56)) return 8;
  if (x < (1ull << 63)) return 9;
  return 10;
}

// Write varint based on pre-calculated length, returns dst + wire_length as a
// convenience
uint8_t* Write(uint64_t x, uint8_t wire_length, uint8_t* dst) {
  assert(wire_length == WireSizeFor(x));
  switch (wire_length) {
      // note carefully that every case in this switch expression falls through
      // to the next case
    case 10:
      dst[9] = static_cast<uint8_t>((x >> 63) | 0x80);
    case 9:
      dst[8] = static_cast<uint8_t>((x >> 56) | 0x80);
    case 8:
      dst[7] = static_cast<uint8_t>((x >> 49) | 0x80);
    case 7:
      dst[6] = static_cast<uint8_t>((x >> 42) | 0x80);
    case 6:
      dst[5] = static_cast<uint8_t>((x >> 35) | 0x80);
    case 5:
      dst[4] = static_cast<uint8_t>((x >> 28) | 0x80);
    case 4:
      dst[3] = static_cast<uint8_t>((x >> 21) | 0x80);
    case 3:
      dst[2] = static_cast<uint8_t>((x >> 14) | 0x80);
    case 2:
      dst[1] = static_cast<uint8_t>((x >> 7) | 0x80);
    case 1:
      dst[0] = static_cast<uint8_t>((x) | 0x80);
  }
  dst[wire_length - 1] &= 0x7f;
  return dst + wire_length;
}

}  // namespace varint
}  // namespace overnet
