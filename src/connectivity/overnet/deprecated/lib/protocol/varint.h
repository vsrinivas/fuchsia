// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace overnet {
namespace varint {

// Return the number of bytes required to represent x
// This result must be passed into Write, and may be cached
uint8_t WireSizeFor(uint64_t x);

// Write varint based on pre-calculated length, returns dst + wire_length as a
// convenience
uint8_t* Write(uint64_t x, uint8_t wire_length, uint8_t* dst);

// Variant of Write that does not need the pre-computed length
inline uint8_t* Write(uint64_t x, uint8_t* dst) { return Write(x, WireSizeFor(x), dst); }

namespace impl {
bool ReadFallback(const uint8_t** bytes, const uint8_t* end, uint64_t* result);
}

// Parse a single varint
inline bool Read(const uint8_t** bytes, const uint8_t* end, uint64_t* result) {
  if (*bytes < end && **bytes < 0x80) {
    *result = *(*bytes)++;
    return true;
  }
  return impl::ReadFallback(bytes, end, result);
}

// What is the maximum number of bytes that could be written in the form:
// (varint_length_prefix) ++ (bytes)
// such that the total length does not exceed fit_to?
uint64_t MaximumLengthWithPrefix(uint64_t fit_to);

// Returns the largest number n, n < x, such that the number of bytes to record
// n is less than the number of bytes to record x (or zero if this is not
// possible)
uint64_t SmallerRecordedNumber(uint64_t x);

}  // namespace varint

}  // namespace overnet
