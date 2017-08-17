// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <arch/ops.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <list.h>
#include <magenta/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

struct percpu {
    /* per cpu timer queue */
    struct list_node timer_queue;

    /* per cpu preemption timer */
    timer_t preempt_timer;

    /* thread/cpu level statistics */
    struct cpu_stats stats;

    /* per cpu idle thread */
    thread_t idle_thread;
} __CPU_MAX_ALIGN;

/* the kernel per-cpu structure */
extern struct percpu percpu[SMP_MAX_CPUS];

static inline struct percpu* get_local_percpu(void) {
    return &percpu[arch_curr_cpu_num()];
}

__END_CDECLS
