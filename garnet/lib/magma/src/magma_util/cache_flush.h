// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CACHE_FLUSH_H
#define CACHE_FLUSH_H

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <cpuid.h>

namespace magma {

class CacheFlush {
public:
    CacheFlush()
    {
        unsigned int a, b, c, d;
        if (__get_cpuid(1, &a, &b, &c, &d)) {
            cacheline_size_ = 8 * ((b >> 8) & 0xff);
        } else {
            DASSERT(false);
        }
        DASSERT(magma::is_pow2(cacheline_size_));
    }

    inline void clflush_range(void* start, size_t size)
    {
        const uint64_t mask = cacheline_size_ - 1;
        uint8_t* p = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(start) & ~mask);
        uint8_t* end = reinterpret_cast<uint8_t*>(start) + size;

        __builtin_ia32_mfence();
        while (p < end) {
            __builtin_ia32_clflush(p);
            p += cacheline_size_;
        }
    }

private:
    uint32_t cacheline_size_ = 64;
};

} // namespace magma

#endif // CACHE_FLUSH_H
