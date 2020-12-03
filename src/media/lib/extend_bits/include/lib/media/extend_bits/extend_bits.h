// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_EXTEND_BITS_INCLUDE_LIB_MEDIA_EXTEND_BITS_EXTEND_BITS_H_
#define SRC_MEDIA_LIB_EXTEND_BITS_INCLUDE_LIB_MEDIA_EXTEND_BITS_EXTEND_BITS_H_

#include <inttypes.h>

// Does not require the wrap iterval of to_extend to be a power of 2.
uint64_t ExtendBitsGeneral(uint64_t nearby_extended, uint64_t to_extend,
                           uint32_t non_extended_modulus);

// Requires the wrap interval of to_extend to be a power of 2.
uint64_t ExtendBits(uint64_t nearby_extended, uint64_t to_extend,
                    uint32_t to_extend_low_order_bit_count);

#endif  // SRC_MEDIA_LIB_EXTEND_BITS_INCLUDE_LIB_MEDIA_EXTEND_BITS_EXTEND_BITS_H_
