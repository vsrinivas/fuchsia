// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#ifndef __BYTE_ORDER__
#error __BYTE_ORDER__ not defined!
#endif

#ifndef __ORDER_LITTLE_ENDIAN__
#error __ORDER_LITTLE_ENDIAN__ not defined!
#endif

uint16_t htons(uint16_t val) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap16(val);
#else
    return val;
#endif
}

uint16_t ntohs(uint16_t val) {
    return htons(val);
}

uint32_t htonl(uint32_t val) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(val);
#else
    return val;
#endif
}

uint32_t ntohl(uint32_t val) {
    return htonl(val);
}

