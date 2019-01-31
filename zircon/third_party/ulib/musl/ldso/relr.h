// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dynlink.h"

__NO_SAFESTACK NO_ASAN static inline void apply_relr(ElfW(Addr) base,
                                                     const ElfW(Addr)* relr,
                                                     size_t relrsz) {
    const size_t nrelr = relrsz / sizeof(relr[0]);
    size_t i = 0;
    while (i < nrelr) {
        ElfW(Addr)* addr = (void*)(base + relr[i++]);
        *addr++ += base;
        while (i < nrelr && (relr[i] & 1)) {
            // Each bitmask word covers the next 63 possible relocations.
            // Each bit is one if run[bit] should be relocated.

            ElfW(Addr)* run = addr;
            addr += (sizeof(relr[0]) * CHAR_BIT) - 1;

            int skip;
            for (ElfW(Addr) bitmask = relr[i++] >> 1;
                 bitmask != 0;
                 bitmask >>= skip) {
                skip = __builtin_ctzl(bitmask) + 1;
                run += skip;
                run[-1] += base;
            }
        }
    }
}
