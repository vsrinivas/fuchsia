// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extend_bits.h"

#include <zircon/assert.h>

uint64_t ExtendBits(uint64_t nearby_extended, uint64_t to_extend, uint32_t to_extend_low_order_bit_count) {
  ZX_DEBUG_ASSERT(to_extend_low_order_bit_count == 64 ||
                  to_extend < (static_cast<uint64_t>(1) << to_extend_low_order_bit_count));
  // Shift up to the top bits of the uint64_t, so we can exploit subtraction that underflows to
  // compute distance regardless of recent overflow of a and/or b.  We could probably also do this
  // by chopping off some top order bits after subtraction, but somehow this makes more sense to me.
  // This way, we're sorta just creating a and b which are each 64 bit counters with 64 bit natural
  // overflow, so we can figure out the logical above/below relationship between nearby_extended and
  // to_extend.
  uint64_t a = nearby_extended << (64 - to_extend_low_order_bit_count);
  uint64_t b = to_extend << (64 - to_extend_low_order_bit_count);
  // Is the distance between a and b smaller if we assume b is logically above a, or if we assume
  // a is logically above b.  We want to assume the option which has a and b closer together in
  // distance on a mod ring, as we don't generally know whether to_extend will be logically above or
  // logically below nearby_extended.
  //
  // One of these will be relatively small, and the other will be huge (or both 0).  Another way to
  // do this is to check if b - a is < 0x8000000000000000.
  uint64_t result;
  if (b - a <= a - b) {
    // offset is logically above (or equal to) last_inserted_offset
    result = nearby_extended + ((b - a) >> (64 - to_extend_low_order_bit_count));
  } else {
    // offset is logically below last_inserted_offset
    result = nearby_extended - ((a - b) >> (64 - to_extend_low_order_bit_count));
  }
  return result;
}
