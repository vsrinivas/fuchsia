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

__BEGIN_CDECLS

/* override of some routines */
static inline void arch_enable_ints(void)
{
    CF;
    __asm__ volatile("sti");
}

static inline void arch_disable_ints(void)
{
    __asm__ volatile("cli");
    CF;
}

static inline bool arch_ints_disabled(void)
{
    x86_flags_t state;

    __asm__ volatile(
#if ARCH_X86_32
        "pushfl;"
        "popl %%eax"
#elif ARCH_X86_64
        "pushfq;"
        "popq %%rax"
#endif
        : "=a" (state)
        :: "memory");

    return !(state & (1<<9));
}

static inline uint32_t arch_cycle_count(void)
{
    return (rdtsc() & 0xffffffff);
}

static inline void arch_spinloop_pause(void)
{
    __asm__ volatile("pause");
}

static inline void arch_spinloop_signal(void)
{
}

#define mb()        __asm__ volatile ("mfence")
#define wmb()       __asm__ volatile ("sfence")
#define rmb()       __asm__ volatile ("lfence")

#define smp_mb()    mb()
#define smp_wmb()   wmb()
#define smp_rmb()   rmb()

__END_CDECLS

#endif // !ASSEMBLY
