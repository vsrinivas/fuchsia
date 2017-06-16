// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>

#include <magenta/compiler.h>
#include "private.h"

mx_status_t _mx_cache_flush(const void* addr, size_t len, uint32_t flags) {
    if (flags == 0 || (flags & ~(MX_CACHE_FLUSH_INSN | MX_CACHE_FLUSH_DATA)))
        return MX_ERR_INVALID_ARGS;

#if defined(__x86_64__)

    // Nothing needs doing for x86.

#elif defined(__aarch64__)

    if (flags & MX_CACHE_FLUSH_INSN) {
        // TODO(mcgrathr): not supported yet
        __builtin_trap();
    }

    if (flags & MX_CACHE_FLUSH_DATA) {
        for (uintptr_t p = (uintptr_t)addr & -((uintptr_t)DATA_CONSTANTS.dcache_line_size);
             p < (uintptr_t)addr + len;
             p += DATA_CONSTANTS.dcache_line_size) {
            // Clean data cache (dc) to point of coherency (cvac).
            __asm__ volatile("dc cvac, %0" :: "r"(p));
        }
    }

#else

# error what architecture?

#endif

    return MX_OK;
}

VDSO_INTERFACE_FUNCTION(mx_cache_flush);
