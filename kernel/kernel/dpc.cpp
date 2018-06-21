// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <list.h>
#include <trace.h>

#include <kernel/dpc.h>
#include <kernel/event.h>
#include <kernel/percpu.h>
#include <kernel/spinlock.h>
#include <lk/init.h>

static spin_lock_t dpc_lock = SPIN_LOCK_INITIAL_VALUE;

zx_status_t dpc_queue(dpc_t* dpc, bool reschedule) {
    DEBUG_ASSERT(dpc);
    DEBUG_ASSERT(dpc->func);

    // disable interrupts before finding lock
    spin_lock_saved_state_t state;
    spin_lock_irqsave(&dpc_lock, state);

    if (list_in_list(&dpc->node)) {
        spin_unlock_irqrestore(&dpc_lock, state);
        return ZX_ERR_ALREADY_EXISTS;
    }

    struct percpu* cpu = get_local_percpu();

    // put the dpc at the tail of the list and signal the worker
    list_add_tail(&cpu->dpc_list, &dpc->node);

    spin_unlock_irqrestore(&dpc_lock, state);

    event_signal(&cpu->dpc_event, reschedule);

    return ZX_OK;
}

zx_status_t dpc_queue_thread_locked(dpc_t* dpc) {
    DEBUG_ASSERT(dpc);
    DEBUG_ASSERT(dpc->func);

    // interrupts are already disabled
    spin_lock(&dpc_lock);

    if (list_in_list(&dpc->node)) {
        spin_unlock(&dpc_lock);
        return ZX_ERR_ALREADY_EXISTS;
    }

    struct percpu* cpu = get_local_percpu();

    // put the dpc at the tail of the list and signal the worker
    list_add_tail(&cpu->dpc_list, &dpc->node);
    event_signal_thread_locked(&cpu->dpc_event);

    spin_unlock(&dpc_lock);

    return ZX_OK;
}

void dpc_shutdown(uint cpu_id) {
    DEBUG_ASSERT(cpu_id < SMP_MAX_CPUS);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&dpc_lock, state);

    DEBUG_ASSERT(!percpu[cpu_id].dpc_stop);

    // Ask the DPC thread to terminate.
    percpu[cpu_id].dpc_stop = true;

    // Take the thread pointer so we can join outside the spinlock.
    thread_t* t = percpu[cpu_id].dpc_thread;
    percpu[cpu_id].dpc_thread = nullptr;

    spin_unlock_irqrestore(&dpc_lock, state);

    // Wake it.
    event_signal(&percpu[cpu_id].dpc_event, false);

    // Wait for it to terminate.
    int ret = 0;
    zx_status_t status = thread_join(t, &ret, ZX_TIME_INFINITE);
    DEBUG_ASSERT(status == ZX_OK);
    DEBUG_ASSERT(ret == 0);
}

void dpc_shutdown_transition_off_cpu(uint cpu_id) {
    DEBUG_ASSERT(cpu_id < SMP_MAX_CPUS);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&dpc_lock, state);

    uint cur_cpu = arch_curr_cpu_num();
    DEBUG_ASSERT(cpu_id != cur_cpu);

    // The DPC thread should already be stopped.
    DEBUG_ASSERT(percpu[cpu_id].dpc_stop);
    DEBUG_ASSERT(percpu[cpu_id].dpc_thread == nullptr);

    list_node_t* src_list = &percpu[cpu_id].dpc_list;
    list_node_t* dst_list = &percpu[cur_cpu].dpc_list;

    dpc_t* dpc;
    while ((dpc = list_remove_head_type(src_list, dpc_t, node))) {
        list_add_tail(dst_list, &dpc->node);
    }

    // Reset the state so we can restart DPC processing if the CPU comes back online.
    DEBUG_ASSERT(list_is_empty(&percpu[cpu_id].dpc_list));
    percpu[cpu_id].dpc_stop = false;
    event_destroy(&percpu[cpu_id].dpc_event);

    spin_unlock_irqrestore(&dpc_lock, state);
}

static int dpc_thread(void* arg) {
    dpc_t dpc_local;

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, SPIN_LOCK_FLAG_INTERRUPTS);

    struct percpu* cpu = get_local_percpu();
    event_t* event = &cpu->dpc_event;
    list_node_t* list = &cpu->dpc_list;

    arch_interrupt_restore(state, SPIN_LOCK_FLAG_INTERRUPTS);

    for (;;) {
        // wait for a dpc to fire
        __UNUSED zx_status_t err = event_wait(event);
        DEBUG_ASSERT(err == ZX_OK);

        spin_lock_irqsave(&dpc_lock, state);

        if (cpu->dpc_stop) {
            spin_unlock_irqrestore(&dpc_lock, state);
            return 0;
        }

        // pop a dpc off the list, make a local copy.
        dpc_t* dpc = list_remove_head_type(list, dpc_t, node);

        // if the list is now empty, unsignal the event so we block until it is
        if (!dpc) {
            event_unsignal(event);
            dpc_local.func = NULL;
        } else {
           dpc_local = *dpc;
        }

        spin_unlock_irqrestore(&dpc_lock, state);

        // call the dpc
        if (dpc_local.func)
            dpc_local.func(&dpc_local);
    }

    return 0;
}

void dpc_init_for_cpu(void) {
    struct percpu* cpu = get_local_percpu();
    uint cpu_num = arch_curr_cpu_num();

    // the cpu's dpc state was initialized on a previous hotplug event
    if (event_initialized(&cpu->dpc_event)) {
        return;
    }

    list_initialize(&cpu->dpc_list);
    event_init(&cpu->dpc_event, false, 0);
    cpu->dpc_stop = false;

    char name[10];
    snprintf(name, sizeof(name), "dpc-%u", cpu_num);
    cpu->dpc_thread = thread_create(name, &dpc_thread, NULL, DPC_THREAD_PRIORITY, DEFAULT_STACK_SIZE);
    thread_set_cpu_affinity(cpu->dpc_thread, cpu_num_to_mask(cpu_num));
    thread_resume(cpu->dpc_thread);
}

static void dpc_init(unsigned int level) {
    // initialize dpc for the main CPU
    dpc_init_for_cpu();
}

LK_INIT_HOOK(dpc, dpc_init, LK_INIT_LEVEL_THREADING);
