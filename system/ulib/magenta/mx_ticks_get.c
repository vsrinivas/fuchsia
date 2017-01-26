// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>

uint64_t _mx_ticks_get() {
#if __aarch64__
    uint64_t ticks;
    __asm__ volatile("mrs %0, pmccntr_el0" : "=r" (ticks));
    return ticks;
#elif __x86_64__
    uint32_t ticks_low;
    uint32_t ticks_high;
    __asm__ volatile("rdtsc" : "=a" (ticks_low), "=d" (ticks_high));
    return ((uint64_t)ticks_high << 32) | ticks_low;
#elif __i386__
    uint64_t ticks;
    __asm__ volatile("rdtsc" : "=A" (ticks));
    return ticks;
#else
#error Unsupported architecture
#endif
}

__typeof(mx_ticks_get) mx_ticks_get
    __attribute__((weak, alias("_mx_ticks_get")));
