// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>

#ifndef ASSEMBLY

#include <arch/x86.h>
#include <arch/x86/mp.h>
#include <kernel/atomic.h>

__BEGIN_CDECLS

/* override of some routines */
static inline void arch_enable_ints(void)
{
    atomic_signal_fence();
    __asm__ volatile("sti");
}

static inline void arch_disable_ints(void)
{
    __asm__ volatile("cli");
    atomic_signal_fence();
}

static inline bool arch_ints_disabled(void)
{
    x86_flags_t state;

    __asm__ volatile(
        "pushfq;"
        "popq %%rax"
        : "=a" (state)
        :: "memory");

    return !(state & (1<<9));
}

static inline uint64_t arch_cycle_count(void)
{
    return rdtsc();
}

static inline void arch_spinloop_pause(void)
{
    __asm__ volatile("pause" ::: "memory");
}

static inline void arch_spinloop_signal(void)
{
}

#define mb()        __asm__ volatile ("mfence" ::: "memory")
#define smp_mb()    mb()

static inline uint32_t arch_dcache_line_size(void) {
    // TODO(mcgrathr): not needed for anything yet
    // cpuid can separately report line sizes for L[123]
    return 0;
}

static inline uint32_t arch_icache_line_size(void) {
    // TODO(mcgrathr): not needed for anything yet
    // cpuid can separately report line sizes for L[123]
    return 0;
}

// Log architecture-specific data for process creation.
// This can only be called after the process has been created and before
// it is running. Alas we can't use mx_koid_t here as the arch layer is at a
// lower level than magenta.
void arch_trace_process_create(uint64_t pid, paddr_t pt_phys);

__END_CDECLS

#endif // !ASSEMBLY
