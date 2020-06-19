// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_patterns.h"

#include <stdint.h>
#include <zircon/assert.h>

namespace hwstress {

namespace {

// Return the right |n| bits of |w|.
inline uint64_t RightNBits(uint64_t w, uint64_t n) {
  if (n >= 64) {
    return w;
  }
  return w & ((1ul << n) - 1ul);
}

}  // namespace

std::vector<uint64_t> RotatePattern(std::vector<uint64_t> v, uint64_t n) {
  ZX_ASSERT(n < 64);

  // Minimal cases.
  if (n == 0) {
    return v;
  }
  if (v.empty()) {
    return v;
  }

  // Get the right-most N bits.
  uint64_t right_bits = RightNBits(v.back(), n);
  for (size_t i = 0; i < v.size(); i++) {
    uint64_t next_right_bits = RightNBits(v[i], n);
    v[i] = (v[i] >> n) | (right_bits << (64 - n));
    right_bits = next_right_bits;
  }

  return v;
}

std::vector<uint64_t> NegateWords(std::vector<uint64_t> v) {
  for (size_t i = 0; i < v.size(); i++) {
    v[i] = ~v[i];
  }
  return v;
}

}  // namespace hwstress
