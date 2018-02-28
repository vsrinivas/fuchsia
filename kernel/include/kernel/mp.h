// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/cpu.h>
#include <kernel/mutex.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef void (*mp_ipi_task_func_t)(void* context);
typedef void (*mp_sync_task_t)(void* context);

/* by default, mp_reschedule does not signal to cpus that are running realtime
 * threads. Override this behavior.
 */
#define MP_RESCHEDULE_FLAG_REALTIME (0x1)
/* by default, mp_reschedule relies on arch_mp_reschedule, which requires that
 * the thread lock be held. This flag can be used to directly send MP_IPI_RESCHEDULE
 * interrupts without needing the thread lock.
 */
#define MP_RESCHEDULE_FLAG_USE_IPI  (0x2)

typedef enum {
    MP_IPI_GENERIC,
    MP_IPI_RESCHEDULE,
    MP_IPI_HALT,
} mp_ipi_t;

/* When sending inter processor interrupts (IPIs), apis will take a combination of
 * this enum and a bitmask. If MP_IPI_TARGET_MASK is used, the mask argument will
 * contain a bitmap of every cpu that should receive the IPI. The other targets
 * serve as shortcuts and potentially optimizations in the lower layers.
 */
typedef enum {
    MP_IPI_TARGET_MASK,
    MP_IPI_TARGET_ALL,
    MP_IPI_TARGET_ALL_BUT_LOCAL
} mp_ipi_target_t;

void mp_init(void);

void mp_prepare_current_cpu_idle_state(bool idle);
void mp_reschedule(cpu_mask_t mask, uint flags);
void mp_sync_exec(mp_ipi_target_t, cpu_mask_t mask, mp_sync_task_t task, void* context);

zx_status_t mp_hotplug_cpu_mask(cpu_mask_t mask);
zx_status_t mp_unplug_cpu_mask(cpu_mask_t mask);
static inline zx_status_t mp_hotplug_cpu(cpu_num_t cpu) {
    return mp_hotplug_cpu_mask(cpu_num_to_mask(cpu));
}
static inline zx_status_t mp_unplug_cpu(cpu_num_t cpu) {
    return mp_unplug_cpu_mask(cpu_num_to_mask(cpu));
}

/* called from arch code during reschedule irq */
void mp_mbx_reschedule_irq(void);
/* called from arch code during generic task irq */
void mp_mbx_generic_irq(void);

/* represents a pending task for some number of CPUs to execute */
struct mp_ipi_task {
    struct list_node node;

    mp_ipi_task_func_t func;
    void* context;
};

/* global mp state to track what the cpus are up to */
struct mp_state {
    /* cpus that are currently online */
    volatile cpu_mask_t online_cpus;
    /* cpus that are currently schedulable */
    volatile cpu_mask_t active_cpus;

    /* only safely accessible with thread lock held */
    cpu_mask_t idle_cpus;
    cpu_mask_t realtime_cpus;

    spin_lock_t ipi_task_lock;
    /* list of outstanding tasks for CPUs to execute.  Should only be
     * accessed with the ipi_task_lock held */
    struct list_node ipi_task_list[SMP_MAX_CPUS];

    /* lock for serializing CPU hotplug/unplug operations */
    mutex_t hotplug_lock;
};

extern struct mp_state mp;

void mp_set_curr_cpu_online(bool online);
void mp_set_curr_cpu_active(bool active);

static inline int mp_is_cpu_active(cpu_num_t cpu) {
    return atomic_load((int*)&mp.active_cpus) & cpu_num_to_mask(cpu);
}

static inline int mp_is_cpu_idle(cpu_num_t cpu) {
    return mp.idle_cpus & cpu_num_to_mask(cpu);
}

static inline int mp_is_cpu_online(cpu_num_t cpu) {
    return mp.online_cpus & cpu_num_to_mask(cpu);
}

/* must be called with the thread lock held */

/* idle/busy is used to track if the cpu is running anything or has a non empty run queue
 * idle == (cpu run queue empty & cpu running idle thread)
 * busy == !idle
 */
static inline void mp_set_cpu_idle(cpu_num_t cpu) {
    mp.idle_cpus |= cpu_num_to_mask(cpu);
}

static inline void mp_set_cpu_busy(cpu_num_t cpu) {
    mp.idle_cpus &= ~cpu_num_to_mask(cpu);
}

static inline cpu_mask_t mp_get_idle_mask(void) {
    return mp.idle_cpus;
}

static inline cpu_mask_t mp_get_active_mask(void) {
    return atomic_load((int*)&mp.active_cpus);
}

static inline cpu_mask_t mp_get_online_mask(void) {
    return mp.online_cpus;
}

static inline void mp_set_cpu_realtime(cpu_num_t cpu) {
    mp.realtime_cpus |= cpu_num_to_mask(cpu);
}

static inline void mp_set_cpu_non_realtime(cpu_num_t cpu) {
    mp.realtime_cpus &= ~cpu_num_to_mask(cpu);
}

static inline cpu_mask_t mp_get_realtime_mask(void) {
    return mp.realtime_cpus;
}

__END_CDECLS
