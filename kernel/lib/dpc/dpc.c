// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <lib/dpc.h>

#include <assert.h>
#include <err.h>
#include <list.h>
#include <trace.h>

#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <lk/init.h>

static spin_lock_t dpc_lock = SPIN_LOCK_INITIAL_VALUE;
static struct list_node dpc_list = LIST_INITIAL_VALUE(dpc_list);
static event_t dpc_event = EVENT_INITIAL_VALUE(dpc_event, false, 0);

status_t dpc_queue(dpc_t *dpc, bool reschedule)
{
    DEBUG_ASSERT(dpc);
    DEBUG_ASSERT(dpc->func);

    if (list_in_list(&dpc->node))
        return NO_ERROR;

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&dpc_lock, state);

    // put the dpc at the tail of the list and signal the worker
    list_add_tail(&dpc_list, &dpc->node);
    event_signal(&dpc_event, false);

    spin_unlock_irqrestore(&dpc_lock, state);

    // reschedule here if asked to
    if (reschedule)
        thread_preempt(false);

    return NO_ERROR;
}

status_t dpc_queue_thread_locked(dpc_t *dpc)
{
    DEBUG_ASSERT(dpc);
    DEBUG_ASSERT(dpc->func);

    if (list_in_list(&dpc->node))
        return NO_ERROR;

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&dpc_lock, state);

    // put the dpc at the tail of the list and signal the worker
    list_add_tail(&dpc_list, &dpc->node);
    event_signal_thread_locked(&dpc_event);

    spin_unlock_irqrestore(&dpc_lock, state);

    return NO_ERROR;
}

static int dpc_thread(void *arg)
{
    for (;;) {
        // wait for a dpc to fire
        __UNUSED status_t err = event_wait(&dpc_event);
        DEBUG_ASSERT(err == NO_ERROR);

        spin_lock_saved_state_t state;
        spin_lock_irqsave(&dpc_lock, state);

        // pop a dpc off the list
        dpc_t *dpc = list_remove_head_type(&dpc_list, dpc_t, node);

        // if the list is now empty, unsignal the event so we block until it is
        if (!dpc)
            event_unsignal(&dpc_event);

        spin_unlock_irqrestore(&dpc_lock, state);

        // call the dpc
        if (dpc && dpc->func)
            dpc->func(dpc);
    }


    return 0;
}

static void dpc_init(unsigned int level)
{
    thread_t *t = thread_create("dpc", &dpc_thread, NULL, HIGH_PRIORITY, DEFAULT_STACK_SIZE);
    thread_detach_and_resume(t);
}

LK_INIT_HOOK(dpc, dpc_init, LK_INIT_LEVEL_THREADING);
