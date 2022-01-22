// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/compiler.h"

#define BITARR_TYPE_NUM_BITS (sizeof(uint64_t) * 8)

// Find the first asserted LSB(assume the input is little-endian).
//
// Returns:
//   [0, num_bits): found. The index of first asserted bit (the least significant one).
//   num_bits: No asserted bit found in num_bits.
//
size_t find_first_bit(unsigned int* bits, const size_t num_bits) {
  const size_t num_of_ints = DIV_ROUND_UP(num_bits, BITS_PER_INT);
  size_t ret = num_bits;

  for (size_t i = 0; i < num_of_ints; ++i) {
    if (bits[i] == 0) {
      continue;
    }
    ret = (i * BITS_PER_INT) + __builtin_ctz(bits[i]);
    break;
  }

  return MIN(num_bits, ret);
}

// Find the last asserted MSB(assume the input is little-endian).
//
// Returns:
//   [0, num_bits): found. The index of last asserted bit (the most significant one).
//   num_bits: No asserted bit found in num_bits.
//
size_t find_last_bit(unsigned int* bits, const size_t num_bits) {
  const size_t num_of_ints = DIV_ROUND_UP(num_bits, BITS_PER_INT);
  const size_t size_inside_int = (num_bits - 1) % BITS_PER_INT + 1;
  unsigned int mask =
      size_inside_int == BITS_PER_INT ? ~0U : (unsigned int)(1 << size_inside_int) - 1;

  size_t ret = num_bits;
  for (int64_t i = (int64_t)(num_of_ints - 1); i >= 0; --i) {
    if (bits[i] == 0) {
      continue;
    }
    unsigned int val = mask & bits[i];

    if (val == 0) {
      continue;
    }
    ret = (i * BITS_PER_INT) + (BITS_PER_INT - 1 - __builtin_clz(val));
    break;
  }

  return MIN(num_bits, ret);
}

// Find next bit which is set in an unsigned integer.
//
// Returns:
//   [0, num_bits): found. The index of next bit which is set to 1 starts from bit_offset.
//   num_bits: No asserted bit found in num_bits.
//
size_t find_next_bit(unsigned int* bitarr, size_t num_bits, size_t bit_offset) {
  if (bit_offset >= num_bits) {
    return num_bits;
  }

  size_t word_offset = bit_offset / BITARR_TYPE_NUM_BITS;
  size_t offset_within_word = bit_offset % BITARR_TYPE_NUM_BITS;

  size_t rest = bitarr[word_offset] & (~0ULL << offset_within_word);
  if (rest != 0) {
    int bit = __builtin_ffsll(rest) - 1;
    return MIN(word_offset * BITARR_TYPE_NUM_BITS + bit, num_bits);
  }

  size_t skipped_bits = (word_offset + 1) * BITARR_TYPE_NUM_BITS;
  if (skipped_bits >= num_bits) {
    return num_bits;
  }

  return skipped_bits + find_first_bit(bitarr + word_offset + 1, num_bits - skipped_bits);
}
