/*
 * Copyright 2018 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bitarr.h"

size_t find_first_bit(const BITARR_TYPE* bitarr, size_t num_bits) {
    size_t n = BITARR_SIZE(num_bits);
    for (size_t i = 0; i < n; ++i) {
        if (bitarr[i] != 0) {
            int bit = __builtin_ffsll(bitarr[i]) - 1;
            return MIN(i * BITARR_TYPE_NUM_BITS + bit, num_bits);
        }
    }
    return num_bits;
}

size_t find_next_bit(const BITARR_TYPE* bitarr, size_t num_bits, size_t bit_offset) {
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

bool bitarr_empty(const BITARR_TYPE* bitarr, size_t num_bits) {
    return find_first_bit(bitarr, num_bits) == num_bits;
}
