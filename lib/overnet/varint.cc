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

uint64_t MaximumLengthWithPrefix(uint64_t x) {
  assert(x > 0);
  uint64_t r = x - WireSizeFor(x);
  while (r + 1 + WireSizeFor(r + 1) < x) r++;
  return r;
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

namespace {
bool ReadFromArray(const uint8_t** bytes, uint64_t* value) {
  const uint8_t* ptr = *bytes;
  uint32_t b;

  // Splitting into 32-bit pieces gives better performance on 32-bit
  // processors.
  uint32_t part0 = 0, part1 = 0, part2 = 0;

  b = *(ptr++);
  part0 = b;
  if (!(b & 0x80)) goto done;
  part0 -= 0x80;
  b = *(ptr++);
  part0 += b << 7;
  if (!(b & 0x80)) goto done;
  part0 -= 0x80 << 7;
  b = *(ptr++);
  part0 += b << 14;
  if (!(b & 0x80)) goto done;
  part0 -= 0x80 << 14;
  b = *(ptr++);
  part0 += b << 21;
  if (!(b & 0x80)) goto done;
  part0 -= 0x80 << 21;
  b = *(ptr++);
  part1 = b;
  if (!(b & 0x80)) goto done;
  part1 -= 0x80;
  b = *(ptr++);
  part1 += b << 7;
  if (!(b & 0x80)) goto done;
  part1 -= 0x80 << 7;
  b = *(ptr++);
  part1 += b << 14;
  if (!(b & 0x80)) goto done;
  part1 -= 0x80 << 14;
  b = *(ptr++);
  part1 += b << 21;
  if (!(b & 0x80)) goto done;
  part1 -= 0x80 << 21;
  b = *(ptr++);
  part2 = b;
  if (!(b & 0x80)) goto done;
  part2 -= 0x80;
  b = *(ptr++);
  part2 += b << 7;
  if (!(b & 0x80)) goto done;
  // "part2 -= 0x80 << 7" is irrelevant because (0x80 << 7) << 56 is 0.

  // We have overrun the maximum size of a varint (10 bytes).  Assume
  // the data is corrupt.
  return false;

done:
  *value = (static_cast<uint64_t>(part0)) |
           (static_cast<uint64_t>(part1) << 28) |
           (static_cast<uint64_t>(part2) << 56);
  *bytes = ptr;
  return true;
}

bool ReadSlow(const uint8_t** bytes, const uint8_t* end, uint64_t* value) {
  // Slow path:  This read might cross the end of the buffer, we fail if it
  // does so

  uint64_t result = 0;
  int count = 0;
  uint32_t b;

  do {
    if (count == 10) {
      *value = 0;
      return false;
    }
    if (*bytes == end) {
      return false;
    }
    b = **bytes;
    result |= static_cast<uint64_t>(b & 0x7f) << (7 * count);
    ++*bytes;
    ++count;
  } while (b & 0x80);

  *value = result;
  return true;
}
}  // namespace

namespace impl {
bool ReadFallback(const uint8_t** bytes, const uint8_t* end, uint64_t* result) {
  if (*bytes - end >= 10 ||
      // Optimization:  We're also safe if the buffer is non-empty and it ends
      // with a byte that would terminate a varint.
      (end > *bytes && !(end[-1] & 0x80))) {
    return ReadFromArray(bytes, result);
  } else {
    return ReadSlow(bytes, end, result);
  }
}
}  // namespace impl

}  // namespace varint
}  // namespace overnet
