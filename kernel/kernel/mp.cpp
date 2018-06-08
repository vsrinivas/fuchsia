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
#include <dev/interrupt.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <inttypes.h>
#include <kernel/align.h>
#include <kernel/dpc.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/sched.h>
#include <kernel/spinlock.h>
#include <kernel/stats.h>
#include <kernel/timer.h>
#include <lk/init.h>
#include <platform.h>
#include <platform/timer.h>
#include <stdlib.h>
#include <trace.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

// a global state structure, aligned on cpu cache line to minimize aliasing
struct mp_state mp __CPU_ALIGN_EXCLUSIVE;

// Helpers used for implementing mp_sync
struct mp_sync_context;
static void mp_sync_task(void* context);

void mp_init(void) {
    mutex_init(&mp.hotplug_lock);
    mp.ipi_task_lock = SPIN_LOCK_INITIAL_VALUE;
    for (uint i = 0; i < fbl::count_of(mp.ipi_task_list); ++i) {
        list_initialize(&mp.ipi_task_list[i]);
    }
}

void mp_prepare_current_cpu_idle_state(bool idle) {
    arch_prepare_current_cpu_idle_state(idle);
}

void mp_reschedule(cpu_mask_t mask, uint flags) {
    // we must be holding the thread lock to access some of the cpu
    // state bitmaps and some arch_mp_reschedule implementations.
    DEBUG_ASSERT(thread_lock_held());

    const cpu_num_t local_cpu = arch_curr_cpu_num();

    LTRACEF("local %u, mask %#x\n", local_cpu, mask);

    // mask out cpus that are not active and the local cpu
    mask &= mp.active_cpus;
    mask &= ~cpu_num_to_mask(local_cpu);

    // mask out cpus that are currently running realtime code
    if ((flags & MP_RESCHEDULE_FLAG_REALTIME) == 0) {
        mask &= ~mp.realtime_cpus;
    }

    LTRACEF("local %u, post mask target now 0x%x\n", local_cpu, mask);

    // if we have no work to do, return
    if (mask == 0)
        return;

    arch_mp_reschedule(mask);
}

void mp_interrupt(mp_ipi_target_t target, cpu_mask_t mask) {
    arch_mp_send_ipi(target, mask, MP_IPI_INTERRUPT);
}

struct mp_sync_context {
    mp_sync_task_t task;
    void* task_context;
    // Mask of which CPUs need to finish the task
    volatile cpu_mask_t outstanding_cpus;
};

static void mp_sync_task(void* raw_context) {
    auto context = reinterpret_cast<mp_sync_context*>(raw_context);
    context->task(context->task_context);
    // use seq-cst atomic to ensure this update is not seen before the
    // side-effects of context->task
    atomic_and((int*)&context->outstanding_cpus, ~cpu_num_to_mask(arch_curr_cpu_num()));
    arch_spinloop_signal();
}

/* @brief Execute a task on the specified CPUs, and block on the calling
 *        CPU until all CPUs have finished the task.
 *
 *  If MP_IPI_TARGET_ALL or MP_IPI_TARGET_ALL_BUT_LOCAL is the target, the online CPU
 *  mask will be used to determine actual targets.
 *
 * Interrupts must be disabled if calling with MP_IPI_TARGET_ALL_BUT_LOCAL as target
 *
 * The callback in |task| will always be called with |arch_in_int_handler()|
 * set to true.
 */
void mp_sync_exec(mp_ipi_target_t target, cpu_mask_t mask, mp_sync_task_t task, void* context) {
    uint num_cpus = arch_max_num_cpus();

    if (target == MP_IPI_TARGET_ALL) {
        mask = mp_get_online_mask();
    } else if (target == MP_IPI_TARGET_ALL_BUT_LOCAL) {
        // targeting all other CPUs but the current one is hazardous
        // if the local CPU may be changed underneath us
        DEBUG_ASSERT(arch_ints_disabled());
        mask = mp_get_online_mask() & ~cpu_num_to_mask(arch_curr_cpu_num());
    }

    // Mask any offline CPUs from target list
    mask &= mp_get_online_mask();

    // disable interrupts so our current CPU doesn't change
    spin_lock_saved_state_t irqstate;
    arch_interrupt_save(&irqstate, SPIN_LOCK_FLAG_INTERRUPTS);
    smp_mb();

    const uint local_cpu = arch_curr_cpu_num();

    // remove self from target lists, since no need to IPI ourselves
    bool targetting_self = !!(mask & cpu_num_to_mask(local_cpu));
    mask &= ~cpu_num_to_mask(local_cpu);

    // create tasks to enqueue (we need one per target due to each containing
    // a linked list node
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

    // enqueue tasks
    spin_lock(&mp.ipi_task_lock);
    cpu_mask_t remaining = mask;
    uint cpu_id = 0;
    while (remaining && cpu_id < num_cpus) {
        if (remaining & 1) {
            list_add_tail(&mp.ipi_task_list[cpu_id], &sync_tasks[cpu_id].node);
        }
        remaining >>= 1;
        cpu_id++;
    }
    spin_unlock(&mp.ipi_task_lock);

    // let CPUs know to begin executing
    __UNUSED zx_status_t status = arch_mp_send_ipi(MP_IPI_TARGET_MASK, mask, MP_IPI_GENERIC);
    DEBUG_ASSERT(status == ZX_OK);

    if (targetting_self) {
        bool previous_in_int_handler = arch_in_int_handler();
        arch_set_in_int_handler(true);
        mp_sync_task(&sync_context);
        arch_set_in_int_handler(previous_in_int_handler);
    }
    smp_mb();

    // we can take interrupts again once we've executed our task
    arch_interrupt_restore(irqstate, SPIN_LOCK_FLAG_INTERRUPTS);

    bool ints_disabled = arch_ints_disabled();
    // wait for all other CPUs to be done with the context
    while (1) {
        // See comment in mp_unplug_trampoline about related CPU hotplug
        // guarantees.
        cpu_mask_t outstanding = atomic_load_relaxed(
            (int*)&sync_context.outstanding_cpus);
        cpu_mask_t online = mp_get_online_mask();
        if ((outstanding & online) == 0) {
            break;
        }

        // If interrupts are still disabled, we need to attempt to process any
        // tasks queued for us in order to prevent deadlock.
        if (ints_disabled) {
            // Optimistically check if our task list has work without the lock.
            // mp_mbx_generic_irq will take the lock and check again.
            if (!list_is_empty(&mp.ipi_task_list[local_cpu])) {
                bool previous_in_int_handler = arch_in_int_handler();
                arch_set_in_int_handler(true);
                mp_mbx_generic_irq();
                arch_set_in_int_handler(previous_in_int_handler);
                continue;
            }
        }

        arch_spinloop_pause();
    }
    smp_mb();

    // make sure the sync_tasks aren't in lists anymore, since they're
    // stack allocated
    spin_lock_irqsave(&mp.ipi_task_lock, irqstate);
    for (uint i = 0; i < num_cpus; ++i) {
        // If a task is still around, it's because the CPU went offline.
        if (list_in_list(&sync_tasks[i].node)) {
            list_delete(&sync_tasks[i].node);
        }
    }
    spin_unlock_irqrestore(&mp.ipi_task_lock, irqstate);
}

static void mp_unplug_trampoline(void) TA_REQ(thread_lock) __NO_RETURN;
static void mp_unplug_trampoline(void) {
    // We're still holding the thread lock from the reschedule that took us
    // here.

    thread_t* ct = get_current_thread();
    auto unplug_done = reinterpret_cast<event_t*>(ct->arg);

    cpu_num_t cpu_num = arch_curr_cpu_num();
    sched_transition_off_cpu(cpu_num);

    // Note that before this invocation, but after we stopped accepting
    // interrupts, we may have received a synchronous task to perform.
    // Clearing this flag will cause the mp_sync_exec caller to consider
    // this CPU done.  If this CPU comes back online before other all
    // of the other CPUs finish their work (very unlikely, since tasks
    // should be quick), then this CPU may execute the task.
    mp_set_curr_cpu_online(false);

    // do *not* enable interrupts, we want this CPU to never receive another
    // interrupt
    spin_unlock(&thread_lock);

    // Stop and then shutdown this CPU's platform timer.
    platform_stop_timer();
    platform_shutdown_timer();

    // Shutdown the interrupt controller for this CPU.  On some platforms (arm64 with GIC) receiving
    // an interrupt at a powered off CPU can result in implementation defined behavior (including
    // resetting the whole system).
    shutdown_interrupts_curr_cpu();

    // flush all of our caches
    arch_flush_state_and_halt(unplug_done);
}

// Hotplug the given cpus.  Blocks until the CPUs are up, or a failure is
// detected.
//
// This should be called in a thread context
zx_status_t mp_hotplug_cpu_mask(cpu_mask_t cpu_mask) {
    DEBUG_ASSERT(!arch_ints_disabled());

    zx_status_t status = ZX_OK;

    mutex_acquire(&mp.hotplug_lock);

    // Make sure all of the requested CPUs are offline
    if (cpu_mask & mp_get_online_mask()) {
        status = ZX_ERR_BAD_STATE;
        goto cleanup_mutex;
    }

    while (cpu_mask != 0) {
        cpu_num_t cpu_id = highest_cpu_set(cpu_mask);
        cpu_mask &= ~cpu_num_to_mask(cpu_id);

        status = platform_mp_cpu_hotplug(cpu_id);
        if (status != ZX_OK) {
            break;
        }
    }
cleanup_mutex:
    mutex_release(&mp.hotplug_lock);
    return status;
}

// Unplug a single CPU.  Must be called while hodling the hotplug lock
static zx_status_t mp_unplug_cpu_mask_single_locked(cpu_num_t cpu_id) {
    // Wait for |cpu_id| to complete any in-progress DPCs and terminate its DPC thread.  Later, once
    // nothing is running on it, we'll migrate its queued DPCs to another CPU.
    dpc_shutdown(cpu_id);

    // TODO(maniscalco): |cpu_id| is about to shutdown.  We should ensure it has no pinned threads
    // (except maybe the idle thread).  Once we're confident we've terminated/migrated them all,
    // this would be a good place to DEBUG_ASSERT.

    // Create a thread for the unplug.  We will cause the target CPU to
    // context switch to this thread.  After this happens, it should no
    // longer be accessing system state and can be safely shut down.
    //
    // This thread is pinned to the target CPU and set to run with the
    // highest priority.  This should cause it to pick up the thread
    // immediately (or very soon, if for some reason there is another
    // HIGHEST_PRIORITY task scheduled in between when we resume the
    // thread and when the CPU is woken up).
    event_t unplug_done = EVENT_INITIAL_VALUE(unplug_done, false, 0);
    thread_t* t = thread_create_etc(
        NULL,
        "unplug_thread",
        NULL,
        &unplug_done,
        HIGHEST_PRIORITY,
        NULL, NULL, 4096,
        mp_unplug_trampoline);
    if (t == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = platform_mp_prep_cpu_unplug(cpu_id);
    if (status != ZX_OK) {
        return status;
    }

    // Pin to the target CPU
    thread_set_cpu_affinity(t, cpu_num_to_mask(cpu_id));
    // Set real time to cancel the pre-emption timer
    thread_set_real_time(t);

    status = thread_detach_and_resume(t);
    if (status != ZX_OK) {
        goto cleanup_thread;
    }

    // Wait for the unplug thread to get scheduled on the target
    do {
        status = event_wait(&unplug_done);
    } while (status < 0);

    // Now that the CPU is no longer processing tasks, move all of its timers
    timer_transition_off_cpu(cpu_id);
    // Move the CPU's queued DPCs to the current CPU.
    dpc_shutdown_transition_off_cpu(cpu_id);

    status = platform_mp_cpu_unplug(cpu_id);
    if (status != ZX_OK) {
        // Do not cleanup the unplug thread in this case.  We have successfully
        // unplugged the CPU from the scheduler's perspective, but the platform
        // may have failed to shut down the CPU
        return status;
    }

    // Fall through.  Since the thread is scheduled, it should not be in any
    // queues.  Since the CPU running this thread is now shutdown, we can just
    // erase the thread's existence.
cleanup_thread:
    thread_forget(t);
    return status;
}

// Unplug the given cpus.  Blocks until the CPUs are removed.  Partial
// failure may occur (in which some CPUs are removed but not others).
//
// This should be called in a thread context
zx_status_t mp_unplug_cpu_mask(cpu_mask_t cpu_mask) {
    DEBUG_ASSERT(!arch_ints_disabled());

    zx_status_t status = ZX_OK;

    mutex_acquire(&mp.hotplug_lock);

    // Make sure all of the requested CPUs are online
    if (cpu_mask & ~mp_get_online_mask()) {
        status = ZX_ERR_BAD_STATE;
        goto cleanup_mutex;
    }

    while (cpu_mask != 0) {
        cpu_num_t cpu_id = highest_cpu_set(cpu_mask);
        cpu_mask &= ~cpu_num_to_mask(cpu_id);

        status = mp_unplug_cpu_mask_single_locked(cpu_id);
        if (status != ZX_OK) {
            break;
        }
    }

cleanup_mutex:
    mutex_release(&mp.hotplug_lock);
    return status;
}

void mp_set_curr_cpu_online(bool online) {
    if (online) {
        atomic_or((volatile int*)&mp.online_cpus, cpu_num_to_mask(arch_curr_cpu_num()));
    } else {
        atomic_and((volatile int*)&mp.online_cpus, ~cpu_num_to_mask(arch_curr_cpu_num()));
    }
}

void mp_set_curr_cpu_active(bool active) {
    if (active) {
        atomic_or((volatile int*)&mp.active_cpus, cpu_num_to_mask(arch_curr_cpu_num()));
    } else {
        atomic_and((volatile int*)&mp.active_cpus, ~cpu_num_to_mask(arch_curr_cpu_num()));
    }
}

void mp_mbx_generic_irq() {
    DEBUG_ASSERT(arch_ints_disabled());
    const cpu_num_t local_cpu = arch_curr_cpu_num();

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
}

void mp_mbx_reschedule_irq() {
    const cpu_num_t cpu = arch_curr_cpu_num();

    LTRACEF("cpu %u\n", cpu);

    CPU_STATS_INC(reschedule_ipis);

    if (mp.active_cpus & cpu_num_to_mask(cpu))
        thread_preempt_set_pending();
}

void mp_mbx_interrupt_irq() {
    const cpu_num_t cpu = arch_curr_cpu_num();

    LTRACEF("cpu %u\n", cpu);

    // do nothing, the entire point of this interrupt is to simply have one
    // delivered to the cpu.
}

__WEAK zx_status_t arch_mp_cpu_hotplug(uint cpu_id) {
    return ZX_ERR_NOT_SUPPORTED;
}
__WEAK zx_status_t arch_mp_prep_cpu_unplug(uint cpu_id) {
    return ZX_ERR_NOT_SUPPORTED;
}
__WEAK zx_status_t arch_mp_cpu_unplug(uint cpu_id) {
    return ZX_ERR_NOT_SUPPORTED;
}
__WEAK zx_status_t platform_mp_cpu_hotplug(uint cpu_id) {
    return arch_mp_cpu_hotplug(cpu_id);
}
__WEAK zx_status_t platform_mp_prep_cpu_unplug(uint cpu_id) {
    return arch_mp_prep_cpu_unplug(cpu_id);
}
__WEAK zx_status_t platform_mp_cpu_unplug(uint cpu_id) {
    return arch_mp_cpu_unplug(cpu_id);
}
