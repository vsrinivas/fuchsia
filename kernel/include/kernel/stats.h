// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

/* per cpu kernel level statistics */
struct cpu_stats {
    zx_duration_t idle_time;
    ulong reschedules;
    ulong context_switches;
    ulong irq_preempts;
    ulong preempts;
    ulong yields;

    /* cpu level interrupts and exceptions */
    ulong interrupts;  /* hardware interrupts, minus timer interrupts or inter-processor interrupts */
    ulong timer_ints;  /* timer interrupts */
    ulong timers;      /* timer callbacks */
    ulong perf_ints;   /* performance monitor interrupts */
    ulong syscalls;

    /* inter-processor interrupts */
    ulong reschedule_ipis;
    ulong generic_ipis;
};

__END_CDECLS

/* include after the cpu_stats definition above, since it is part of the percpu structure */
#include <kernel/percpu.h>

#define CPU_STATS_INC(name)                                                        \
    do {                                                                           \
        __atomic_fetch_add(&get_local_percpu()->stats.name, 1u, __ATOMIC_RELAXED); \
    } while (0)
