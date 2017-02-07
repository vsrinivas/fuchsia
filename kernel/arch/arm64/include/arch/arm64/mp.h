// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/arm64.h>
#include <magenta/compiler.h>
#include <reg.h>

__BEGIN_CDECLS

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
    return (uint)   (   ((mpidr & ((1U << (SMP_CPU_CLUSTER_BITS + SMP_CPU_CLUSTER_SHIFT)) - 1)  ) >> 8) << SMP_CPU_ID_BITS) | ((uint)mpidr & 0xff);
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

