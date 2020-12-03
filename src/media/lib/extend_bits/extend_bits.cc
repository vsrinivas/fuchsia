// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/extend_bits/extend_bits.h"

#include <zircon/assert.h>

#include <cstdint>

namespace {

struct ResultVsNearbyExtendedCase {
  // true considers a case where result is above nearby_extended (in same epoch as nearby_extended
  // or the next epoch above nearby_extended).  false considers a case where result is below
  // nearby_extended (in the epoch below nearby_extended or the same epoch as nearby_extended).
  bool is_result_above;
  // 0 is the epoch below nearby_extended.  1 is the same epoch as nearby_extended.  2 is the epoch
  // above nearby_extended.
  uint32_t relative_epoch_index;
};

// These are all the cases we need to consider.  Whichever case results in the lowest unsigned diff
// (in the appropriate direction), is the appropriate/correct placement for result.
//
// By definition, we know which epoch nearby_extended is in.  If result is above nearby_extended,
// then result may be in the same epoch or the epoch above.  If result is below nearby_extended,
// then result may be in the epoch below or the same epoch.  This means we can find result by
// considering 4 cases and using the case that results in the smallest unsigned diff between result
// and nearby_extended.
//
// We don't need to consider result being above nearby_extended but having epoch_index 0, nor do we
// need to consider result being below nearby_extended by having epoch_index 2, so those 2 cases are
// intentionally missing from this array (and that's why we have this array instead of just
// enumerating the cases in code with 2 nested for loops).
const ResultVsNearbyExtendedCase result_vs_nearby_cases[] = {
    {true, 1},   // result may be above, in same epoch as nearby_extended
    {true, 2},   // result may be above, in next epoch above nearby_extended
    {false, 0},  // result may be below, in epoch just below nearby_extended
    {false, 1},  // result may be below, in same epoch as nearby_extended
};

}  // namespace

// Does not require modulus to be a power of 2.  We avoid doing a % b where a or b are negative, to
// hopefully make this more readable.
//
// The goal is to find result such that result is as close as possible to nearby_extended while
// satisfying result % non_extended_modulus == to_extend.
//
// Since pow(2, 64) is not a multiple of non_extended_modulus (so uint64_t overflow isn't going to
// be seamless with regard to non_extended_modulus epoch), we restrict result to be in the same
// uint64_t overflow epoch as nearby_extended.
//
// If non_extended_modulus is known to be a power of 2, consider using ExtendBits() instead, which
// correctly handles uint64_t epoch wrapping (not that such overflow will/can happen without a reset
// of the relevant counter before then in most usage cases), and is likely faster.
uint64_t ExtendBitsGeneral(uint64_t nearby_extended, uint64_t to_extend,
                           uint32_t non_extended_modulus) {
  ZX_DEBUG_ASSERT(non_extended_modulus > 0);
  ZX_DEBUG_ASSERT(to_extend < non_extended_modulus);
  uint64_t nearby_epoch_index = nearby_extended / non_extended_modulus;
  // nearby_non_extended_adjusted is in relative epoch index 1 (instead of relative epoch index 0).
  uint64_t nearby_non_extended_adjusted =
      nearby_extended % non_extended_modulus + non_extended_modulus;

  // Find limits for relative_epoch_index such that the result will be in the same uint64_t epoch
  // as nearby_extended.
  uint32_t min_relative_epoch_index = 0;
  uint32_t max_relative_epoch_index = 2;
  if (nearby_epoch_index == 0) {
    min_relative_epoch_index = 1;
  }
  uint64_t end_of_the_line_non_extended = -1ull % non_extended_modulus;
  uint64_t end_of_the_line_epoch_index = -1ull / non_extended_modulus;
  if (nearby_epoch_index == end_of_the_line_epoch_index) {
    if (to_extend > end_of_the_line_non_extended) {
      max_relative_epoch_index = 0;
    } else {
      max_relative_epoch_index = 1;
    }
  } else if (nearby_epoch_index + 1 == end_of_the_line_epoch_index) {
    if (to_extend > end_of_the_line_non_extended) {
      max_relative_epoch_index = 1;
    } else {
      max_relative_epoch_index = 2;
    }
  }

  uint64_t best_case_index_so_far = -1ull;
  uint64_t min_diff_so_far = -1ull;
  for (uint32_t case_index = 0; case_index < countof(result_vs_nearby_cases); ++case_index) {
    const auto& a_case = result_vs_nearby_cases[case_index];
    if (a_case.relative_epoch_index < min_relative_epoch_index ||
        a_case.relative_epoch_index > max_relative_epoch_index) {
      continue;
    }
    uint64_t to_extend_adjusted = to_extend + non_extended_modulus * a_case.relative_epoch_index;
    uint64_t diff;
    if (a_case.is_result_above) {
      // consider result above nearby_extended
      diff = to_extend_adjusted - nearby_non_extended_adjusted;
    } else {
      // consider result below nearby_extended
      diff = nearby_non_extended_adjusted - to_extend_adjusted;
    }
    if (diff < min_diff_so_far) {
      min_diff_so_far = diff;
      best_case_index_so_far = case_index;
    }
  }
  ZX_DEBUG_ASSERT(best_case_index_so_far != -1ull);
  ZX_DEBUG_ASSERT(min_diff_so_far != -1ull);
  const auto& best_case = result_vs_nearby_cases[best_case_index_so_far];
  uint64_t result =
      (nearby_epoch_index + best_case.relative_epoch_index - 1) * non_extended_modulus + to_extend;
  return result;
}

// This requires the modulus to be a power of 2.
uint64_t ExtendBits(uint64_t nearby_extended, uint64_t to_extend,
                    uint32_t to_extend_low_order_bit_count) {
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
