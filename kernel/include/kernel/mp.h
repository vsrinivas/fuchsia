// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <limits.h>
#include <magenta/compiler.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

typedef uint32_t mp_cpu_mask_t;
typedef void (*mp_ipi_task_func_t)(void* context);
typedef void (*mp_sync_task_t)(void* context);

static_assert(SMP_MAX_CPUS <= sizeof(mp_cpu_mask_t) * CHAR_BIT, "");

/* by default, mp_mbx_reschedule does not signal to cpus that are running realtime
 * threads. Override this behavior.
 */
#define MP_RESCHEDULE_FLAG_REALTIME (0x1)

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

void mp_reschedule(mp_ipi_target_t, mp_cpu_mask_t mask, uint flags);
void mp_sync_exec(mp_ipi_target_t, mp_cpu_mask_t mask, mp_sync_task_t task, void* context);

status_t mp_hotplug_cpu(uint cpu_id);
status_t mp_unplug_cpu(uint cpu_id);

/* called from arch code during reschedule irq */
enum handler_return mp_mbx_reschedule_irq(void);
/* called from arch code during generic task irq */
enum handler_return mp_mbx_generic_irq(void);

/* represents a pending task for some number of CPUs to execute */
struct mp_ipi_task {
    struct list_node node;

    mp_ipi_task_func_t func;
    void* context;
};

/* global mp state to track what the cpus are up to */
struct mp_state {
    /* cpus that are currently online */
    volatile mp_cpu_mask_t online_cpus;
    /* cpus that are currently schedulable */
    volatile mp_cpu_mask_t active_cpus;

    /* only safely accessible with thread lock held */
    mp_cpu_mask_t idle_cpus;
    mp_cpu_mask_t realtime_cpus;

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

static inline int mp_is_cpu_active(uint cpu) {
    return atomic_load((int*)&mp.active_cpus) & (1 << cpu);
}

static inline int mp_is_cpu_idle(uint cpu) {
    return mp.idle_cpus & (1 << cpu);
}

static inline int mp_is_cpu_online(uint cpu) {
    return mp.online_cpus & (1 << cpu);
}

/* must be called with the thread lock held */
static inline void mp_set_cpu_idle(uint cpu) {
    mp.idle_cpus |= 1U << cpu;
}

static inline void mp_set_cpu_busy(uint cpu) {
    mp.idle_cpus &= ~(1U << cpu);
}

static inline mp_cpu_mask_t mp_get_idle_mask(void) {
    return mp.idle_cpus;
}

static inline mp_cpu_mask_t mp_get_active_mask(void) {
    return atomic_load((int*)&mp.active_cpus);
}

static inline mp_cpu_mask_t mp_get_online_mask(void) {
    return mp.online_cpus;
}

static inline void mp_set_cpu_realtime(uint cpu) {
    mp.realtime_cpus |= 1U << cpu;
}

static inline void mp_set_cpu_non_realtime(uint cpu) {
    mp.realtime_cpus &= ~(1U << cpu);
}

static inline mp_cpu_mask_t mp_get_realtime_mask(void) {
    return mp.realtime_cpus;
}

__END_CDECLS
