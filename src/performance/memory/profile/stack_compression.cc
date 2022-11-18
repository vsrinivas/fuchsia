// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/memory/profile/stack_compression.h"

#include <lib/stdcompat/span.h>
#include <stdlib.h>

namespace {

void to_varint(uint64_t value, cpp20::span<uint8_t>::iterator& out) {
  if (value <= 0x7f) {
    *out++ = (value & 0x7f);  // Mask 7 bits.
    return;
  }
  *out++ = (value & 0x7f) | 0x80;
  to_varint(value >> 7, out);  // Encode the rest with tail recursion.
}

uint64_t from_varint(cpp20::span<const uint8_t>::iterator& out) {
  const uint8_t b = *out++;
  if (b <= 0x7f) {
    return b;
  }
  return (b & 0x7f) | (from_varint(out) << 7);  // Decode upper part with tail recursion.
}

}  // namespace

cpp20::span<uint8_t> compress(cpp20::span<const uint64_t> values, cpp20::span<uint8_t> out) {
  assert(out.size() >= values.size() * 9);
  uint64_t previous = 0;
  auto out_itr = out.begin();
  for (auto value : values) {
    to_varint(value ^ previous, out_itr);
    previous = value;
  }
  return {out.begin(), out_itr};
}

cpp20::span<uint64_t> decompress(cpp20::span<const uint8_t> in, cpp20::span<uint64_t> values) {
  uint64_t previous = 0;
  auto out_values = values.begin();
  for (auto in_itr = in.begin(); in_itr != in.end();) {
    *out_values++ = (previous ^= from_varint(in_itr));
  }
  return {values.begin(), out_values};
}
