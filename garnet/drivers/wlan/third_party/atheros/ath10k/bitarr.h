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

#ifndef GARNET_DRIVERS_WLAN_THIRD_PARTY_ATHEROS_ATH10K_BITARR_H_
#define GARNET_DRIVERS_WLAN_THIRD_PARTY_ATHEROS_ATH10K_BITARR_H_

#include <stdbool.h>
#include <stdint.h>
#include "macros.h"

typedef uint64_t BITARR_TYPE;

#define BITARR_TYPE_NUM_BITS (sizeof(BITARR_TYPE) * 8)
#define BITARR_SIZE(num_bits) (DIV_ROUNDUP((num_bits), BITARR_TYPE_NUM_BITS))
#define BITARR(name, num_bits) BITARR_TYPE name[BITARR_SIZE(num_bits)]
#define BITARR_SET(name, bit) \
    (name)[(bit) / BITARR_TYPE_NUM_BITS] |= ((BITARR_TYPE)1 << ((bit) % BITARR_TYPE_NUM_BITS))
#define BITARR_CLEAR(name, bit) \
    (name)[(bit) / BITARR_TYPE_NUM_BITS] &= ~((BITARR_TYPE)1 << ((bit) % BITARR_TYPE_NUM_BITS))
#define BITARR_TEST(name, bit)               \
    (((name)[(bit) / BITARR_TYPE_NUM_BITS] & \
      ((BITARR_TYPE)1 << ((bit) % BITARR_TYPE_NUM_BITS))) != 0)

size_t find_first_bit(const BITARR_TYPE* bitarr, size_t num_bits);

size_t find_next_bit(const BITARR_TYPE* bitarr, size_t num_bits, size_t bit_offset);

bool bitarr_empty(const BITARR_TYPE* bitarr, size_t num_bits);

#define for_each_set_bit(bit, bitarr, num_bits)                            \
    for ((bit) = find_first_bit((bitarr), (num_bits)); (bit) < (num_bits); \
         (bit) = find_next_bit((bitarr), (num_bits), (bit) + 1))

#endif  // GARNET_DRIVERS_WLAN_THIRD_PARTY_ATHEROS_ATH10K_BITARR_H_
