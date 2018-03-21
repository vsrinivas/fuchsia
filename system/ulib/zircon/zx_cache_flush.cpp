// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <zircon/compiler.h>
#include "private.h"

namespace {

template<typename T>
void for_each_cache_line(const void* addr, size_t len, uint32_t line_size,
                         T func) {
    for (uintptr_t p = (uintptr_t)addr & -(uintptr_t)line_size;
         p < (uintptr_t)addr + len;
         p += line_size) {
        func(p);
    }
}

template<typename T>
void for_each_dcache_line(const void* addr, size_t len, T func) {
    for_each_cache_line(addr, len, DATA_CONSTANTS.dcache_line_size, func);
}

template<typename T>
void for_each_icache_line(const void* addr, size_t len, T func) {
    for_each_cache_line(addr, len, DATA_CONSTANTS.icache_line_size, func);
}

}  // anonymous namespace

zx_status_t _zx_cache_flush(const void* addr, size_t len, uint32_t flags) {
    switch (flags) {
    case ZX_CACHE_FLUSH_INSN:
        break;
    case ZX_CACHE_FLUSH_DATA:
        break;
    case ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INSN:
        break;
    case ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE:
        break;
    case ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE | ZX_CACHE_FLUSH_INSN:
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

#if defined(__x86_64__)

    unsigned cacheline_size = DATA_CONSTANTS.dcache_line_size;
    if (cacheline_size == 0) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    const uint64_t mask = cacheline_size - 1;
    uint8_t* p = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(addr) & ~mask);
    const uint8_t* end = reinterpret_cast<const uint8_t*>(addr) + len;

    // TODO: Use clflushopt if available
    // TODO: Use clwb if available and we weren't asked to invalidate
    while (p < end) {
        __asm__ volatile("clflush %0" ::"m"(*p));
        p += cacheline_size;
    }
    __asm__ volatile("mfence");

#elif defined(__aarch64__)

    if (flags & ZX_CACHE_FLUSH_DATA) {
        // Flush data to the point of coherency which effectively means
        // making sure cache data is written back to main mmemory and optionally
        // invalidated.
        if (flags & ZX_CACHE_FLUSH_INVALIDATE) {
            for_each_dcache_line(addr, len, [](uintptr_t p) {
                    // Clean and invalidate data cache to point of coherency.
                    __asm__ volatile("dc civac, %0" :: "r"(p));
                });
        } else {
            for_each_dcache_line(addr, len, [](uintptr_t p) {
                    // Clean data cache (dc) to point of coherency (cvac).
                    __asm__ volatile("dc cvac, %0" :: "r"(p));
                });
        }
        // Ensure the cache flush has completed with regards to point of coherency
        __asm__ volatile("dsb ish");
    }

    if (flags & ZX_CACHE_FLUSH_INSN) {
        // If we didn't already clean the dcache all the way to the point
        // of coherency, clean it the point to unification.
        // Point of unification is the level within the cache heirarchy where the
        // the instruction and data cache are no longer separate. This is usually L2.
        if (!(flags & ZX_CACHE_FLUSH_DATA)) {
            for_each_dcache_line(addr, len, [](uintptr_t p) {
                    // Clean data cache (dc) to point of unification (cvau).
                    __asm__ volatile("dc cvau, %0" :: "r"(p));
                });
            // Synchronize the dcache flush to before the icache flush.
            __asm__ volatile("dsb ish");
        }

        for_each_icache_line(addr, len, [](uintptr_t p) {
                // Invalidate instruction cache (ic) to point of unification (ivau).
                __asm__ volatile("ic ivau, %0" :: "r"(p));
            });
        // Synchronize the icache flush to before future instruction fetches.
        __asm__ volatile("isb sy");
    }

#else

# error what architecture?

#endif

    return ZX_OK;
}

VDSO_INTERFACE_FUNCTION(zx_cache_flush);
