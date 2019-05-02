// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#ifndef __ASSEMBLER__

#include <arch/arm64.h>
#include <arch/arm64/feature.h>
#include <arch/arm64/interrupt.h>
#include <arch/arm64/mp.h>
#include <reg.h>
#include <stdbool.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

#define ENABLE_CYCLE_COUNTER 1

static inline void arch_spinloop_pause(void) {
    __yield();
}

#define mb() __dsb(ARM_MB_SY)
#define smp_mb() __dmb(ARM_MB_SY)

static inline uint64_t arch_cycle_count(void) {
    return __arm_rsr64("pmccntr_el0");
}

static inline uint32_t arch_cpu_features(void) {
    return arm64_features;
}

static inline uint32_t arch_dcache_line_size(void) {
    return arm64_dcache_size;
}

static inline uint32_t arch_icache_line_size(void) {
    return arm64_icache_size;
}

// Log architecture-specific data for process creation.
// This can only be called after the process has been created and before
// it is running. Alas we can't use zx_koid_t here as the arch layer is at a
// lower level than zircon.
static inline void arch_trace_process_create(uint64_t pid, paddr_t tt_phys) {
    // nothing to do
}

static inline bool arch_cas_16_acquire(volatile unsigned __int128* dst,
                                       volatile unsigned __int128* expected,
                                       unsigned __int128 desired) {
    int result;
    do {
        uint64_t temp_lo;
        uint64_t temp_hi;
        __asm__ volatile("ldaxp %[lo], %[hi], %[src]"
                         : [ lo ] "=r"(temp_lo), [ hi ] "=r"(temp_hi)
                         : [ src ] "Q"(*dst));
        unsigned __int128 temp = ((unsigned __int128)(temp_hi)) << 64 | temp_lo;
        if (temp != *expected) {
            *expected = temp;
            return false;
        }
        __asm__ volatile("stxp %w[result], %[lo], %[hi], %[dst]"
                         : [ result ] "=&r"(result), [ dst ] "=Q"(*dst)
                         : [ lo ] "r"((uint64_t)(desired)), [ hi ] "r"((uint64_t)(desired >> 64))
                         : "memory");
    } while (unlikely(result));
    return true;
}

__END_CDECLS

#endif // __ASSEMBLER__
