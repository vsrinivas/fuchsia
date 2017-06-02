// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <list.h>
#include <arch/ops.h>
#include <magenta/compiler.h>
#include <sys/types.h>
#include <kernel/thread.h>
#include <kernel/timer.h>

__BEGIN_CDECLS

struct percpu {
    /* per cpu timer queue */
    struct list_node timer_queue;

#if PLATFORM_HAS_DYNAMIC_TIMER
    /* per cpu preemption timer */
    timer_t preempt_timer;
#endif

    /* thread/cpu level statistics */
    struct thread_stats {
        lk_time_t idle_time;
        lk_time_t last_idle_timestamp;
        ulong reschedules;
        ulong context_switches;
        ulong irq_preempts;
        ulong preempts;
        ulong yields;

        /* cpu level interrupts and exceptions */
        ulong interrupts; /* hardware interrupts, minus timer interrupts or inter-processor interrupts */
        ulong timer_ints; /* timer interrupts */
        ulong timers; /* timer callbacks */
        ulong exceptions; /* exceptions such as page fault or undefined opcode */
        ulong syscalls;

        /* inter-processor interrupts */
        ulong reschedule_ipis;
        ulong generic_ipis;
    } thread_stats;

    /* per cpu idle thread */
    thread_t idle_thread;
} __CPU_MAX_ALIGN;

/* the kernel per-cpu structure */
extern struct percpu percpu[SMP_MAX_CPUS];

static inline struct percpu *get_local_percpu(void) {
    return &percpu[arch_curr_cpu_num()];
}

__END_CDECLS
