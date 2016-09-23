// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#ifndef ASSEMBLY

#include <stdbool.h>
#include <magenta/compiler.h>
#include <reg.h>
#include <arch/arm64.h>

__BEGIN_CDECLS

#define ENABLE_CYCLE_COUNTER 1

// override of some routines
static inline void arch_enable_ints(void)
{
    CF;
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

static inline void arch_disable_ints(void)
{
    __asm__ volatile("msr daifset, #2" ::: "memory");
    CF;
}

static inline bool arch_ints_disabled(void)
{
    unsigned int state;

    __asm__ volatile("mrs %0, daif" : "=r"(state));
    state &= (1<<7);

    return !!state;
}

static inline void arch_enable_fiqs(void)
{
    CF;
    __asm__ volatile("msr daifclr, #1" ::: "memory");
}

static inline void arch_disable_fiqs(void)
{
    __asm__ volatile("msr daifset, #1" ::: "memory");
    CF;
}

// XXX
static inline bool arch_fiqs_disabled(void)
{
    unsigned int state;

    __asm__ volatile("mrs %0, daif" : "=r"(state));
    state &= (1<<6);

    return !!state;
}

static inline void arch_spinloop_pause(void)
{
    __asm__ volatile("wfe");
}

static inline void arch_spinloop_signal(void)
{
    __asm__ volatile("sev");
}

#define mb()        __asm__ volatile("dsb sy" : : : "memory")
#define rmb()       __asm__ volatile("dsb ld" : : : "memory")
#define wmb()       __asm__ volatile("dsb st" : : : "memory")

#ifdef WITH_SMP
#define smp_mb()    __asm__ volatile("dmb ish" : : : "memory")
#define smp_rmb()   __asm__ volatile("dmb ishld" : : : "memory")
#define smp_wmb()   __asm__ volatile("dmb ishst" : : : "memory")
#else
#define smp_mb()    CF
#define smp_wmb()   CF
#define smp_rmb()   CF
#endif

static inline uint32_t arch_cycle_count(void)
{
    return (uint32_t)ARM64_READ_SYSREG(pmccntr_el0);
}

/* use the cpu local thread context pointer to store current_thread */
static inline struct thread *get_current_thread(void)
{
    return (struct thread *)ARM64_READ_SYSREG(tpidr_el1);
}

static inline void set_current_thread(struct thread *t)
{
    ARM64_WRITE_SYSREG(tpidr_el1, (uint64_t)t);
}

#if WITH_SMP
static inline uint arch_curr_cpu_num(void)
{
    uint64_t mpidr =  ARM64_READ_SYSREG(mpidr_el1);
    return ((mpidr & ((1U << SMP_CPU_ID_BITS) - 1)) >> 8 << SMP_CPU_CLUSTER_SHIFT) | (mpidr & 0xff);
}
static inline uint arch_max_num_cpus(void)
{
    extern uint arm_num_cpus;

    return arm_num_cpus;
}
#else
static inline uint arch_curr_cpu_num(void)
{
    return 0;
}
static inline uint arch_max_num_cpus(void)
{
    return 1;
}
#endif

static inline bool arch_in_int_handler(void)
{
    extern bool arm64_in_int_handler[SMP_MAX_CPUS];

    return arm64_in_int_handler[arch_curr_cpu_num()];
}

__END_CDECLS

#endif // ASSEMBLY

