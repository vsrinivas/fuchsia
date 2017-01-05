// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <kernel/sched.h>

#include <debug.h>
#include <assert.h>
#include <inttypes.h>
#include <list.h>
#include <string.h>
#include <printf.h>
#include <err.h>
#include <kernel/mp.h>
#include <kernel/thread.h>

/* the run queue */
static struct list_node run_queue[NUM_PRIORITIES];
static uint32_t run_queue_bitmap;

/* make sure the bitmap is large enough to cover our number of priorities */
static_assert(NUM_PRIORITIES <= sizeof(run_queue_bitmap) * CHAR_BIT, "");

/* find a cpu to wake up */
static mp_cpu_mask_t find_cpu(thread_t *t)
{
    return MP_CPU_ALL_BUT_LOCAL;
}

/* run queue manipulation */
static void insert_in_run_queue_head(thread_t *t)
{
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(t->state == THREAD_READY);
    DEBUG_ASSERT(!list_in_list(&t->queue_node));
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    list_add_head(&run_queue[t->priority], &t->queue_node);
    run_queue_bitmap |= (1<<t->priority);
}

static void insert_in_run_queue_tail(thread_t *t)
{
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(t->state == THREAD_READY);
    DEBUG_ASSERT(!list_in_list(&t->queue_node));
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    list_add_tail(&run_queue[t->priority], &t->queue_node);
    run_queue_bitmap |= (1<<t->priority);
}

thread_t *sched_get_top_thread(uint cpu)
{
    thread_t *newthread;
    uint32_t local_run_queue_bitmap = run_queue_bitmap;

    while (local_run_queue_bitmap) {
        /* find the first (remaining) queue with a thread in it */
        uint next_queue = HIGHEST_PRIORITY - __builtin_clz(local_run_queue_bitmap)
                          - (sizeof(run_queue_bitmap) * CHAR_BIT - NUM_PRIORITIES);

        list_for_every_entry(&run_queue[next_queue], newthread, thread_t, queue_node) {
#if WITH_SMP
            if (likely(newthread->pinned_cpu < 0) || (uint)newthread->pinned_cpu == cpu)
#endif
            {
                list_delete(&newthread->queue_node);

                if (list_is_empty(&run_queue[next_queue]))
                    run_queue_bitmap &= ~(1<<next_queue);

                return newthread;
            }
        }

        local_run_queue_bitmap &= ~(1<<next_queue);
    }
    /* no threads to run, select the idle thread for this cpu */
    return &idle_threads[cpu];
}

void sched_block(void)
{
    __UNUSED thread_t *current_thread = get_current_thread();

    DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    DEBUG_ASSERT(current_thread->state != THREAD_RUNNING);

    // XXX deal with time slice fiddling here

    /* we are blocking on something. the blocking code should have already stuck us on a queue */
    thread_resched();
}

void sched_unblock(thread_t *t, bool resched)
{
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    /* if we're instructed to reschedule, stick the current thread on the head
     * of the run queue first, so that the newly awakened thread gets a chance to run
     * before the current one, but the current one doesn't get unnecessarilly punished.
     */
    if (resched) {
        thread_t *current_thread = get_current_thread();

        current_thread->state = THREAD_READY;
        insert_in_run_queue_head(current_thread);
    }

    /* stuff the new thread in the run queue */
    t->state = THREAD_READY;
    insert_in_run_queue_head(t);

    mp_reschedule(find_cpu(t), 0);

    if (resched)
        thread_resched();
}

void sched_unblock_list(struct list_node *list, bool resched)
{
    DEBUG_ASSERT(list);
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    /* if we're instructed to reschedule, stick the current thread on the head
     * of the run queue first, so that the newly awakened thread gets a chance to run
     * before the current one, but the current one doesn't get unnecessarilly punished.
     */
    if (resched) {
        thread_t *current_thread = get_current_thread();

        current_thread->state = THREAD_READY;
        insert_in_run_queue_head(current_thread);
    }

    /* pop the list of threads and shove into the scheduler */
    thread_t *t;
    while ((t = list_remove_tail_type(list, thread_t, queue_node))) {
        DEBUG_ASSERT(t->magic == THREAD_MAGIC);
        DEBUG_ASSERT(!thread_is_idle(t));

        /* stuff the new thread in the run queue */
        t->state = THREAD_READY;
        insert_in_run_queue_head(t);

        mp_reschedule(find_cpu(t), 0);
    }

    if (resched)
        thread_resched();
}

void sched_yield(void)
{
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    /* we are yielding the cpu, so stick ourselves into the tail of the run queue and reschedule */
    thread_t *current_thread = get_current_thread();
    current_thread->state = THREAD_READY;
    current_thread->remaining_time_slice = 0;
    if (likely(!thread_is_idle(current_thread))) { /* idle thread doesn't go in the run queue */
        insert_in_run_queue_tail(current_thread);
    }
    thread_resched();
}

void sched_preempt(void)
{
    thread_t *current_thread = get_current_thread();

    /* we are being preempted, so we get to go back into the front of the run queue if we have quantum left */
    current_thread->state = THREAD_READY;
    if (likely(!thread_is_idle(current_thread))) { /* idle thread doesn't go in the run queue */
        if (current_thread->remaining_time_slice > 0)
            insert_in_run_queue_head(current_thread);
        else
            insert_in_run_queue_tail(current_thread); /* if we're out of quantum, go to the tail of the queue */
    }
    sched_block();
}

void sched_init_early(void)
{
    /* initialize the run queues */
    for (int i=0; i < NUM_PRIORITIES; i++)
        list_initialize(&run_queue[i]);
}

