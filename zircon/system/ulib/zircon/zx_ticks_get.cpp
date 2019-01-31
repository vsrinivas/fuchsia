// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

#ifdef __x86_64__
#include <x86intrin.h>
#endif

zx_ticks_t _zx_ticks_get(void) {
#if __aarch64__
    // read the virtual counter
    zx_ticks_t ticks;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r" (ticks));
    return ticks;
#elif __x86_64__
    return __rdtsc();
#else
#error Unsupported architecture
#endif
}

VDSO_INTERFACE_FUNCTION(zx_ticks_get);

// At boot time the kernel can decide to redirect the {_,}zx_ticks_get
// dynamic symbol table entries to point to this instead.  See VDso::VDso.
VDSO_KERNEL_EXPORT zx_ticks_t CODE_soft_ticks_get(void) {
    return VDSO_zx_clock_get_monotonic();
}
