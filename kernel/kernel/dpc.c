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

static spin_lock_t dpc_locks[SMP_MAX_CPUS];

zx_status_t dpc_queue(dpc_t* dpc, bool reschedule) {
    DEBUG_ASSERT(dpc);
    DEBUG_ASSERT(dpc->func);

    if (list_in_list(&dpc->node))
        return ZX_OK;

    // disable interrupts before finding lock
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, SPIN_LOCK_FLAG_INTERRUPTS);
    spin_lock_t* lock = &dpc_locks[arch_curr_cpu_num()];
    spin_lock(lock);

    struct percpu* cpu = get_local_percpu();

    // put the dpc at the tail of the list and signal the worker
    list_add_tail(&cpu->dpc_list, &dpc->node);

    spin_unlock(lock);
    arch_interrupt_restore(state, SPIN_LOCK_FLAG_INTERRUPTS);

    event_signal(&cpu->dpc_event, reschedule);

    return ZX_OK;
}

zx_status_t dpc_queue_thread_locked(dpc_t* dpc) {
    DEBUG_ASSERT(dpc);
    DEBUG_ASSERT(dpc->func);

    if (list_in_list(&dpc->node))
        return ZX_OK;

    // interrupts are already disabled
    spin_lock_t* lock = &dpc_locks[arch_curr_cpu_num()];
    spin_lock(lock);

    struct percpu* cpu = get_local_percpu();

    // put the dpc at the tail of the list and signal the worker
    list_add_tail(&cpu->dpc_list, &dpc->node);
    event_signal_thread_locked(&cpu->dpc_event);

    spin_unlock(lock);

    return ZX_OK;
}

void dpc_transition_off_cpu(uint cpu_id) {
    DEBUG_ASSERT(cpu_id < SMP_MAX_CPUS);

    list_node_t temp_list = LIST_INITIAL_VALUE(temp_list);

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, SPIN_LOCK_FLAG_INTERRUPTS);
    spin_lock_t* lock = &dpc_locks[cpu_id];
    spin_lock(lock);

    // move all DPCs from cpu_id's list to a temp list
    list_move(&percpu[cpu_id].dpc_list, &temp_list);

    spin_unlock(lock);

    uint cur_cpu = arch_curr_cpu_num();
    DEBUG_ASSERT(cpu_id != cur_cpu);
    lock = &dpc_locks[cur_cpu];
    list_node_t* dst_list = &percpu[cur_cpu].dpc_list;

    spin_lock(lock);

    dpc_t* dpc;
    while ((dpc = list_remove_head_type(&temp_list, dpc_t, node))) {
        list_add_tail(dst_list, &dpc->node);
    }
    spin_unlock(lock);
    arch_interrupt_restore(state, SPIN_LOCK_FLAG_INTERRUPTS);

    event_signal(&percpu[cur_cpu].dpc_event, false);
}

static int dpc_thread(void* arg) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, SPIN_LOCK_FLAG_INTERRUPTS);

    struct percpu* cpu = get_local_percpu();
    event_t* event = &cpu->dpc_event;
    list_node_t* list = &cpu->dpc_list;
    spin_lock_t* lock = &dpc_locks[arch_curr_cpu_num()];

    arch_interrupt_restore(state, SPIN_LOCK_FLAG_INTERRUPTS);

    for (;;) {
        // wait for a dpc to fire
        __UNUSED zx_status_t err = event_wait(event);
        DEBUG_ASSERT(err == ZX_OK);

        spin_lock_irqsave(lock, state);

        // pop a dpc off the list
        dpc_t* dpc = list_remove_head_type(list, dpc_t, node);

        // if the list is now empty, unsignal the event so we block until it is
        if (!dpc)
            event_unsignal(event);

        spin_unlock_irqrestore(lock, state);

        // call the dpc
        if (dpc && dpc->func)
            dpc->func(dpc);
    }

    return 0;
}

void dpc_init_for_cpu(void) {
    struct percpu* cpu = get_local_percpu();
    uint cpu_num = arch_curr_cpu_num();

    list_initialize(&cpu->dpc_list);
    event_init(&cpu->dpc_event, false, 0);
    arch_spin_lock_init(&dpc_locks[cpu_num]);

    char name[10];
    snprintf(name, sizeof(name), "dpc-%u", cpu_num);
    thread_t* t = thread_create(name, &dpc_thread, NULL, DPC_THREAD_PRIORITY, DEFAULT_STACK_SIZE);
    thread_set_cpu_affinity(t, cpu_num_to_mask(cpu_num));
    thread_detach_and_resume(t);
}

static void dpc_init(unsigned int level) {
    // initialize dpc for the main CPU
    dpc_init_for_cpu();
}

LK_INIT_HOOK(dpc, dpc_init, LK_INIT_LEVEL_THREADING);
