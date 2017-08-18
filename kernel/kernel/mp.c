// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/mp.h>

#include <arch/mp.h>
#include <arch/ops.h>
#include <assert.h>
#include <debug.h>
#include <err.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/stats.h>
#include <kernel/timer.h>
#include <stdlib.h>
#include <trace.h>

#define LOCAL_TRACE 0

/* a global state structure, aligned on cpu cache line to minimize aliasing */
struct mp_state mp __CPU_ALIGN = {
    .hotplug_lock = MUTEX_INITIAL_VALUE(mp.hotplug_lock),
    .ipi_task_lock = SPIN_LOCK_INITIAL_VALUE,
};

/* Helpers used for implementing mp_sync */
struct mp_sync_context;
static void mp_sync_task(void* context);

void mp_init(void) {
    mp.ipi_task_lock = SPIN_LOCK_INITIAL_VALUE;
    for (uint i = 0; i < countof(mp.ipi_task_list); ++i) {
        list_initialize(&mp.ipi_task_list[i]);
    }
}

void mp_reschedule(mp_ipi_target_t target, mp_cpu_mask_t mask, uint flags) {
    uint local_cpu = arch_curr_cpu_num();

    LTRACEF("local %u, target %u, mask %#x\n", local_cpu, target, mask);

    switch (target) {
        case MP_IPI_TARGET_ALL:
        case MP_IPI_TARGET_ALL_BUT_LOCAL:
            arch_mp_send_ipi(target, 0, MP_IPI_RESCHEDULE);
            break;
        case MP_IPI_TARGET_MASK:
            if (mask == 0)
                return;

            /* mask out cpus that are not active and the local cpu */
            mask &= mp.active_cpus;
            mask &= ~(1U << local_cpu);

            /* mask out cpus that are currently running realtime code */
            if ((flags & MP_RESCHEDULE_FLAG_REALTIME) == 0) {
                mask &= ~mp.realtime_cpus;
            }

            LTRACEF("local %u, post mask target now 0x%x\n", local_cpu, mask);

            arch_mp_send_ipi(MP_IPI_TARGET_MASK, mask, MP_IPI_RESCHEDULE);
            break;
    }
}

struct mp_sync_context {
    mp_sync_task_t task;
    void* task_context;
    /* Mask of which CPUs need to finish the task */
    volatile mp_cpu_mask_t outstanding_cpus;
};

static void mp_sync_task(void* raw_context) {
    struct mp_sync_context* context = raw_context;
    context->task(context->task_context);
    /* use seq-cst atomic to ensure this update is not seen before the
     * side-effects of context->task */
    atomic_and((int*)&context->outstanding_cpus, ~(1U << arch_curr_cpu_num()));
    arch_spinloop_signal();
}

/* @brief Execute a task on the specified CPUs, and block on the calling
 *        CPU until all CPUs have finished the task.
 *
 *  If MP_IPI_TARGET_ALL or MP_IPI_TARGET_ALL_BUT_LOCAL is the target, the online CPU
 *  mask will be used to determine actual targets.
 *
 * Interrupts must be disabled if calling with MP_IPI_TARGET_ALL_BUT_LOCAL as target
 */
void mp_sync_exec(mp_ipi_target_t target, mp_cpu_mask_t mask, mp_sync_task_t task, void* context) {
    uint num_cpus = arch_max_num_cpus();

    if (target == MP_IPI_TARGET_ALL) {
        mask = mp_get_online_mask();
    } else if (target == MP_IPI_TARGET_ALL_BUT_LOCAL) {
        /* targeting all other CPUs but the current one is hazardous
         * if the local CPU may be changed underneath us */
        DEBUG_ASSERT(arch_ints_disabled());
        mask = mp_get_online_mask() & ~(1U << arch_curr_cpu_num());
    }

    /* Mask any offline CPUs from target list */
    mask &= mp_get_online_mask();

    /* disable interrupts so our current CPU doesn't change */
    spin_lock_saved_state_t irqstate;
    arch_interrupt_save(&irqstate, SPIN_LOCK_FLAG_INTERRUPTS);
    smp_mb();

    uint local_cpu = arch_curr_cpu_num();

    /* remove self from target lists, since no need to IPI ourselves */
    bool targetting_self = !!(mask & (1U << local_cpu));
    mask &= ~(1U << local_cpu);

    /* create tasks to enqueue (we need one per target due to each containing
     * a linked list node */
    struct mp_sync_context sync_context = {
        .task = task,
        .task_context = context,
        .outstanding_cpus = mask,
    };

    struct mp_ipi_task sync_tasks[SMP_MAX_CPUS] = {};
    for (uint i = 0; i < num_cpus; ++i) {
        sync_tasks[i].func = mp_sync_task;
        sync_tasks[i].context = &sync_context;
    }

    /* enqueue tasks */
    spin_lock(&mp.ipi_task_lock);
    mp_cpu_mask_t remaining = mask;
    uint cpu_id = 0;
    while (remaining && cpu_id < num_cpus) {
        if (remaining & 1) {
            list_add_tail(&mp.ipi_task_list[cpu_id], &sync_tasks[cpu_id].node);
        }
        remaining >>= 1;
        cpu_id++;
    }
    spin_unlock(&mp.ipi_task_lock);

    /* let CPUs know to begin executing */
    __UNUSED status_t status = arch_mp_send_ipi(MP_IPI_TARGET_MASK, mask, MP_IPI_GENERIC);
    DEBUG_ASSERT(status == MX_OK);

    if (targetting_self) {
        mp_sync_task(&sync_context);
    }
    smp_mb();

    /* we can take interrupts again once we've executed our task */
    arch_interrupt_restore(irqstate, SPIN_LOCK_FLAG_INTERRUPTS);

    bool ints_disabled = arch_ints_disabled();
    /* wait for all other CPUs to be done with the context */
    while (1) {
        /* See comment in mp_unplug_trampoline about related CPU hotplug
         * guarantees. */
        mp_cpu_mask_t outstanding = atomic_load_relaxed(
            (int*)&sync_context.outstanding_cpus);
        mp_cpu_mask_t online = mp_get_online_mask();
        if ((outstanding & online) == 0) {
            break;
        }

        /* If interrupts are still disabled, we need to attempt to process any
         * tasks queued for us in order to prevent deadlock. */
        if (ints_disabled) {
            /* Optimistically check if our task list has work without the lock.
             * mp_mbx_generic_irq will take the lock and check again */
            if (!list_is_empty(&mp.ipi_task_list[local_cpu])) {
                mp_mbx_generic_irq();
                continue;
            }
        }

        arch_spinloop_pause();
    }
    smp_mb();

    /* make sure the sync_tasks aren't in lists anymore, since they're
     * stack allocated */
    spin_lock_irqsave(&mp.ipi_task_lock, irqstate);
    for (uint i = 0; i < num_cpus; ++i) {
        /* If a task is still around, it's because the CPU went offline. */
        if (list_in_list(&sync_tasks[i].node)) {
            list_delete(&sync_tasks[i].node);
        }
    }
    spin_unlock_irqrestore(&mp.ipi_task_lock, irqstate);
}

static void mp_unplug_trampoline(void) __NO_RETURN;
static void mp_unplug_trampoline(void) {
    /* release the thread lock that was implicitly held across the reschedule */
    spin_unlock(&thread_lock);

    /* do *not* enable interrupts, we want this CPU to never receive another
     * interrupt */

    thread_t* ct = get_current_thread();
    event_t* unplug_done = ct->arg;

    mp_set_curr_cpu_active(false);

    /* Note that before this invocation, but after we stopped accepting
     * interrupts, we may have received a synchronous task to perform.
     * Clearing this flag will cause the mp_sync_exec caller to consider
     * this CPU done.  If this CPU comes back online before other all
     * of the other CPUs finish their work (very unlikely, since tasks
     * should be quick), then this CPU may execute the task. */
    mp_set_curr_cpu_online(false);

    /* flush all of our caches */
    arch_flush_state_and_halt(unplug_done);
}

/* Hotplug the given cpu.  Blocks until the CPU is up, or a failure is
 * detected.
 *
 * This should be called in a thread context
 */
status_t mp_hotplug_cpu(uint cpu_id) {
    DEBUG_ASSERT(!arch_ints_disabled());

    status_t status = MX_ERR_INTERNAL;

    mutex_acquire(&mp.hotplug_lock);

    if (mp_is_cpu_online(cpu_id)) {
        status = MX_ERR_BAD_STATE;
        goto cleanup_mutex;
    }

    status = platform_mp_cpu_hotplug(cpu_id);
cleanup_mutex:
    mutex_release(&mp.hotplug_lock);
    return status;
}

/* Unplug the given cpu.  Blocks until the CPU is removed.
 *
 * This should be called in a thread context
 */
status_t mp_unplug_cpu(uint cpu_id) {
    DEBUG_ASSERT(!arch_ints_disabled());

    thread_t* t = NULL;
    status_t status = MX_ERR_INTERNAL;

    mutex_acquire(&mp.hotplug_lock);

    if (!mp_is_cpu_online(cpu_id)) {
        /* Cannot unplug offline CPU */
        status = MX_ERR_BAD_STATE;
        goto cleanup_mutex;
    }

    /* Create a thread for the unplug.  We will cause the target CPU to
     * context switch to this thread.  After this happens, it should no
     * longer be accessing system state and can be safely shut down.
     *
     * This thread is pinned to the target CPU and set to run with the
     * highest priority.  This should cause it to pick up the thread
     * immediately (or very soon, if for some reason there is another
     * HIGHEST_PRIORITY task scheduled in between when we resume the
     * thread and when the CPU is woken up).
     */
    event_t unplug_done = EVENT_INITIAL_VALUE(unplug_done, false, 0);
    t = thread_create_etc(
        NULL,
        "unplug_thread",
        NULL,
        &unplug_done,
        HIGHEST_PRIORITY,
        NULL, NULL, 4096,
        mp_unplug_trampoline);
    if (t == NULL) {
        status = MX_ERR_NO_MEMORY;
        goto cleanup_mutex;
    }

    status = platform_mp_prep_cpu_unplug(cpu_id);
    if (status != MX_OK) {
        goto cleanup_thread;
    }

    /* Pin to the target CPU */
    thread_set_pinned_cpu(t, cpu_id);
    /* Set real time to cancel the pre-emption timer */
    thread_set_real_time(t);

    status = thread_detach_and_resume(t);
    if (status != MX_OK) {
        goto cleanup_thread;
    }

    /* Wait for the unplug thread to get scheduled on the target */
    do {
        status = event_wait(&unplug_done);
    } while (status < 0);

    /* Now that the CPU is no longer processing tasks, move all of its timers */
    timer_transition_off_cpu(cpu_id);

    status = platform_mp_cpu_unplug(cpu_id);
    if (status != MX_OK) {
        /* Do not cleanup the unplug thread in this case.  We have successfully
         * unplugged the CPU from the scheduler's perspective, but the platform
         * may have failed to shut down the CPU */
        goto cleanup_mutex;
    }

/* Fall through.  Since the thread is scheduled, it should not be in any
     * queues.  Since the CPU running this thread is now shutdown, we can just
     * erase the thread's existence. */
cleanup_thread:
    thread_forget(t);
cleanup_mutex:
    mutex_release(&mp.hotplug_lock);
    return status;
}

void mp_set_curr_cpu_online(bool online) {
    if (online) {
        atomic_or((volatile int*)&mp.online_cpus, 1U << arch_curr_cpu_num());
    } else {
        atomic_and((volatile int*)&mp.online_cpus, ~(1U << arch_curr_cpu_num()));
    }
}

void mp_set_curr_cpu_active(bool active) {
    if (active) {
        atomic_or((volatile int*)&mp.active_cpus, 1U << arch_curr_cpu_num());
    } else {
        atomic_and((volatile int*)&mp.active_cpus, ~(1U << arch_curr_cpu_num()));
    }
}

enum handler_return mp_mbx_generic_irq(void) {
    DEBUG_ASSERT(arch_ints_disabled());
    uint local_cpu = arch_curr_cpu_num();

    CPU_STATS_INC(generic_ipis);

    while (1) {
        struct mp_ipi_task* task;
        spin_lock(&mp.ipi_task_lock);
        task = list_remove_head_type(&mp.ipi_task_list[local_cpu], struct mp_ipi_task, node);
        spin_unlock(&mp.ipi_task_lock);
        if (task == NULL) {
            break;
        }

        task->func(task->context);
    }
    return INT_NO_RESCHEDULE;
}

enum handler_return mp_mbx_reschedule_irq(void) {
    uint cpu = arch_curr_cpu_num();

    LTRACEF("cpu %u\n", cpu);

    CPU_STATS_INC(reschedule_ipis);

    return (mp.active_cpus & (1U << cpu)) ? INT_RESCHEDULE : INT_NO_RESCHEDULE;
}

__WEAK status_t arch_mp_cpu_hotplug(uint cpu_id) {
    return MX_ERR_NOT_SUPPORTED;
}
__WEAK status_t arch_mp_prep_cpu_unplug(uint cpu_id) {
    return MX_ERR_NOT_SUPPORTED;
}
__WEAK status_t arch_mp_cpu_unplug(uint cpu_id) {
    return MX_ERR_NOT_SUPPORTED;
}
__WEAK status_t platform_mp_cpu_hotplug(uint cpu_id) {
    return arch_mp_cpu_hotplug(cpu_id);
}
__WEAK status_t platform_mp_prep_cpu_unplug(uint cpu_id) {
    return arch_mp_prep_cpu_unplug(cpu_id);
}
__WEAK status_t platform_mp_cpu_unplug(uint cpu_id) {
    return arch_mp_cpu_unplug(cpu_id);
}
