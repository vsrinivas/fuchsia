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

// bits for mpidr register
#define MPIDR_AFF0_MASK     0xFFULL
#define MPIDR_AFF0_SHIFT    0
#define MPIDR_AFF1_MASK     0xFF00ULL
#define MPIDR_AFF1_SHIFT    8

#define ARM64_MPID(cluster, cpu) (((cluster << MPIDR_AFF1_SHIFT) & MPIDR_AFF1_MASK) | \
                                  ((cpu << MPIDR_AFF0_SHIFT) & MPIDR_AFF0_MASK))

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

void arch_init_cpu_map(uint cluster_count, uint* cluster_cpus);

static inline uint arch_curr_cpu_num(void)
{
    extern uint arm64_cpu_map[SMP_CPU_MAX_CLUSTERS][SMP_CPU_MAX_CLUSTER_CPUS];

    uint64_t mpidr = ARM64_READ_SYSREG(mpidr_el1);
    uint cluster = (mpidr & MPIDR_AFF1_MASK) >> MPIDR_AFF1_SHIFT;
    uint cpu = (mpidr & MPIDR_AFF0_MASK) >> MPIDR_AFF0_SHIFT;

    return arm64_cpu_map[cluster][cpu];
}

static inline uint arch_max_num_cpus(void)
{
    extern uint arm_num_cpus;

    return arm_num_cpus;
}

static inline uint arch_cpu_num_to_cluster_id(uint cpu)
{
    extern uint arm64_cpu_cluster_ids[SMP_MAX_CPUS];

    return arm64_cpu_cluster_ids[cpu];
}

static inline uint arch_cpu_num_to_cpu_id(uint cpu)
{
    extern uint arm64_cpu_cpu_ids[SMP_MAX_CPUS];

    return arm64_cpu_cpu_ids[cpu];
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
static inline uint arch_cpu_num_to_cluster_id(uint cpu)
{
    return 0;
}
static inline uint arch_cpu_num_to_cpu_id(uint cpu)
{
    return cpu;
}
#endif

static inline bool arch_in_int_handler(void)
{
    extern bool arm64_in_int_handler[SMP_MAX_CPUS];

    return arm64_in_int_handler[arch_curr_cpu_num()];
}

__END_CDECLS

