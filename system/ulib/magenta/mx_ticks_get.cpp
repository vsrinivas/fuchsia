// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>

#include "private.h"

uint64_t _mx_ticks_get(void) {
#if __aarch64__
    uint64_t ticks;
    __asm__ volatile("mrs %0, pmccntr_el0" : "=r" (ticks));
    return ticks;
#elif __x86_64__
    uint32_t ticks_low;
    uint32_t ticks_high;
    __asm__ volatile("rdtsc" : "=a" (ticks_low), "=d" (ticks_high));
    return ((uint64_t)ticks_high << 32) | ticks_low;
#else
#error Unsupported architecture
#endif
}

VDSO_PUBLIC_ALIAS(mx_ticks_get);

// At boot time the kernel can decide to redirect the {_,}mx_ticks_get
// dynamic symbol table entries to point to this instead.  See VDso::VDso.
VDSO_KERNEL_EXPORT uint64_t CODE_soft_ticks_get(void) {
    return VDSO_mx_time_get(MX_CLOCK_MONOTONIC);
}
