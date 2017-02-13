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
#include <lib/ktrace.h>
#include <kernel/mp.h>
#include <kernel/thread.h>

/* legacy implementation that just broadcast ipis for every reschedule */
#define BROADCAST_RESCHEDULE 0

/* disable priority boosting */
#define NO_BOOST 0

#define MAX_PRIORITY_ADJ 4 /* +/- priority levels from the base priority */

/* ktraces just local to this file */
#define LOCAL_KTRACE 0

#if LOCAL_KTRACE
#define LOCAL_KTRACE0(probe) ktrace_probe0(probe)
#define LOCAL_KTRACE2(probe, x, y) ktrace_probe2(probe, x, y)
#else
#define LOCAL_KTRACE0(probe)
#define LOCAL_KTRACE2(probe, x, y)
#endif

/* the run queue */
static struct list_node run_queue[NUM_PRIORITIES];
static uint32_t run_queue_bitmap;

/* make sure the bitmap is large enough to cover our number of priorities */
static_assert(NUM_PRIORITIES <= sizeof(run_queue_bitmap) * CHAR_BIT, "");

/* compute the effective priority of a thread */
static int effec_priority(const thread_t *t)
{
    int ep = t->base_priority + t->priority_boost;
    DEBUG_ASSERT(ep >= LOWEST_PRIORITY && ep <= HIGHEST_PRIORITY);
    return ep;
}

/* boost the priority of the thread by +1 */
static void boost_thread(thread_t *t)
{
    if (NO_BOOST)
        return;

    if (unlikely(thread_is_real_time_or_idle(t)))
        return;

    if (t->priority_boost < MAX_PRIORITY_ADJ &&
        likely((t->base_priority + t->priority_boost) < HIGHEST_PRIORITY)) {
        t->priority_boost++;
    }
}

/* deboost the priority of the thread by -1.
 * If deboosting because the thread is using up all of its time slice,
 * then allow the boost to go negative, otherwise only deboost to 0.
 */
static void deboost_thread(thread_t *t, bool quantum_expiration)
{
    if (NO_BOOST)
        return;

    if (unlikely(thread_is_real_time_or_idle(t)))
        return;

    int boost_floor;
    if (quantum_expiration) {
        /* deboost into negative boost */
        boost_floor = -MAX_PRIORITY_ADJ;

        /* make sure we dont deboost a thread too far */
        if (unlikely(t->base_priority + boost_floor < LOWEST_PRIORITY))
            boost_floor = t->base_priority - LOWEST_PRIORITY;

    } else {
        /* otherwise only deboost to 0 */
        boost_floor = 0;
    }

    /* if we're already bottomed out or below bottomed out, leave it alone */
    if (t->priority_boost <= boost_floor)
        return;

    /* drop a level */
    t->priority_boost--;
}

/* pick a 'random' cpu */
static mp_cpu_mask_t rand_cpu(const mp_cpu_mask_t mask)
{
    if (unlikely(mask == 0))
        return 0;

    /* check that the mask passed in has at least one bit set in the online mask */
    mp_cpu_mask_t online = mp_get_online_mask();
    if (unlikely((mask & online) == 0))
        return 0;

    /* compute the highest online cpu */
    uint highest_cpu = (sizeof(mp_cpu_mask_t) * CHAR_BIT - 1) - __builtin_clz(online);

    /* not very random, round robins a bit through the mask until it gets a hit */
    for (;;) {
        /* protected by THREAD_LOCK, safe to use non atomically */
        static uint rot = 0;

        if (++rot > highest_cpu)
            rot = 0;

        if ((1u << rot) & mask)
            return (1u << rot);
    }
}

/* find a cpu to wake up */
static mp_cpu_mask_t find_cpu(thread_t *t)
{
#if BROADCAST_RESCHEDULE
    return MP_CPU_ALL_BUT_LOCAL;
#elif WITH_SMP
    /* get the last cpu the thread ran on */
    mp_cpu_mask_t last_ran_cpu_mask = (1u << thread_last_cpu(t));

    /* the current cpu */
    mp_cpu_mask_t curr_cpu_mask = (1u << arch_curr_cpu_num());

    /* get a list of idle cpus */
    mp_cpu_mask_t idle_cpu_mask = mp_get_idle_mask();
    if (idle_cpu_mask != 0) {
        if (idle_cpu_mask & curr_cpu_mask) {
            /* the current cpu is idle, so run it here */
            return 0;
        }

        if (last_ran_cpu_mask & idle_cpu_mask) {
            /* the last core it ran on is idle and isn't the current cpu */
            return last_ran_cpu_mask;
        }

        /* pick an idle_cpu */
        return rand_cpu(idle_cpu_mask);
    }

    /* no idle cpus */
    if (last_ran_cpu_mask == curr_cpu_mask) {
        /* the last cpu it ran on is us */
        /* pick a random cpu that isn't the current one */
        return rand_cpu(mp_get_online_mask() & ~(curr_cpu_mask));
    } else {
        /* pick the last cpu it ran on */
        return last_ran_cpu_mask;
    }
#else /* !WITH_SMP */
    /* no smp, dont send an IPI */
    return 0;
#endif
}

/* run queue manipulation */
static void insert_in_run_queue_head(thread_t *t)
{
    DEBUG_ASSERT(!list_in_list(&t->queue_node));

    int ep = effec_priority(t);

    list_add_head(&run_queue[ep], &t->queue_node);
    run_queue_bitmap |= (1u << ep);
}

static void insert_in_run_queue_tail(thread_t *t)
{
    DEBUG_ASSERT(!list_in_list(&t->queue_node));

    int ep = effec_priority(t);

    list_add_tail(&run_queue[ep], &t->queue_node);
    run_queue_bitmap |= (1u << ep);
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

                LOCAL_KTRACE2("sched_get_top", newthread->priority_boost, newthread->base_priority);

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
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    __UNUSED thread_t *current_thread = get_current_thread();

    DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(current_thread->state != THREAD_RUNNING);

    LOCAL_KTRACE0("sched_block");

    /* we are blocking on something. the blocking code should have already stuck us on a queue */
    _thread_resched_internal();
}

void sched_unblock(thread_t *t)
{
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    DEBUG_ASSERT(t->magic == THREAD_MAGIC);

    LOCAL_KTRACE0("sched_unblock");

    /* thread is being woken up, boost its priority */
    boost_thread(t);

    /* stuff the new thread in the run queue */
    t->state = THREAD_READY;
    insert_in_run_queue_head(t);

    mp_reschedule(find_cpu(t), 0);
}

void sched_unblock_list(struct list_node *list)
{
    DEBUG_ASSERT(list);
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    LOCAL_KTRACE0("sched_unblock_list");

    /* pop the list of threads and shove into the scheduler */
    thread_t *t;
    while ((t = list_remove_tail_type(list, thread_t, queue_node))) {
        DEBUG_ASSERT(t->magic == THREAD_MAGIC);
        DEBUG_ASSERT(!thread_is_idle(t));

        /* thread is being woken up, boost its priority */
        boost_thread(t);

        /* stuff the new thread in the run queue */
        t->state = THREAD_READY;
        insert_in_run_queue_head(t);

        mp_reschedule(find_cpu(t), 0);
    }
}

void sched_yield(void)
{
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    thread_t *current_thread = get_current_thread();
    DEBUG_ASSERT(!thread_is_idle(current_thread));

    LOCAL_KTRACE0("sched_yield");

    current_thread->state = THREAD_READY;

    /* consume the rest of the time slice, deboost ourself, and go to the end of the queue */
    current_thread->remaining_time_slice = 0;
    deboost_thread(current_thread, false);
    insert_in_run_queue_tail(current_thread);

    _thread_resched_internal();
}

/* the current thread is being preempted from interrupt context */
void sched_preempt(void)
{
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    thread_t *current_thread = get_current_thread();

    LOCAL_KTRACE0("sched_preempt");

    current_thread->state = THREAD_READY;

    /* idle thread doesn't go in the run queue */
    if (likely(!thread_is_idle(current_thread))) {
        if (current_thread->remaining_time_slice > 0) {
            insert_in_run_queue_head(current_thread);
        } else {
            /* if we're out of quantum, deboost the thread and put it at the tail of the queue */
            deboost_thread(current_thread, true);
            insert_in_run_queue_tail(current_thread);
        }
    }

    _thread_resched_internal();
}

/* the current thread is voluntarily reevaluating the scheduler on the current cpu */
void sched_reschedule(void)
{
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    thread_t *current_thread = get_current_thread();

    LOCAL_KTRACE0("sched_reschedule");

    current_thread->state = THREAD_READY;

    /* idle thread doesn't go in the run queue */
    if (likely(!thread_is_idle(current_thread))) {

        /* deboost the current thread */
        deboost_thread(current_thread, false);

        if (current_thread->remaining_time_slice > 0) {
            insert_in_run_queue_head(current_thread);
        } else {
            insert_in_run_queue_tail(current_thread);
        }
    }

    _thread_resched_internal();
}

void sched_init_early(void)
{
    /* initialize the run queues */
    for (unsigned int i=0; i < NUM_PRIORITIES; i++)
        list_initialize(&run_queue[i]);
}

