// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <kernel/sched.h>

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <lib/ktrace.h>
#include <list.h>
#include <platform.h>
#include <printf.h>
#include <string.h>
#include <target.h>
#include <trace.h>
#include <vm/vm.h>
#include <zircon/types.h>

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

#define LOCAL_TRACE 0

#define DEBUG_THREAD_CONTEXT_SWITCH 0

#define TRACE_CONTEXT_SWITCH(str, x...)  \
    do {                                 \
        if (DEBUG_THREAD_CONTEXT_SWITCH) \
            printf("CS " str, ##x);      \
    } while (0)

/* threads get 10ms to run before they use up their time slice and the scheduler is invoked */
#define THREAD_INITIAL_TIME_SLICE ZX_MSEC(10)

static bool local_migrate_if_needed(thread_t* curr_thread);

/* compute the effective priority of a thread */
static void compute_effec_priority(thread_t* t) {
    int ep = t->base_priority + t->priority_boost;
    if (t->inherited_priority > ep)
        ep = t->inherited_priority;

    DEBUG_ASSERT(ep >= LOWEST_PRIORITY && ep <= HIGHEST_PRIORITY);

    t->effec_priority = ep;
}

/* boost the priority of the thread by +1 */
static void boost_thread(thread_t* t) {
    if (NO_BOOST)
        return;

    if (unlikely(thread_is_real_time_or_idle(t)))
        return;

    if (t->priority_boost < MAX_PRIORITY_ADJ &&
        likely((t->base_priority + t->priority_boost) < HIGHEST_PRIORITY)) {
        t->priority_boost++;
        compute_effec_priority(t);
    }
}

/* deboost the priority of the thread by -1.
 * If deboosting because the thread is using up all of its time slice,
 * then allow the boost to go negative, otherwise only deboost to 0.
 */
static void deboost_thread(thread_t* t, bool quantum_expiration) {
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
    compute_effec_priority(t);
}

/* pick a 'random' cpu out of the passed in mask of cpus */
static cpu_mask_t rand_cpu(cpu_mask_t mask) {
    if (unlikely(mask == 0))
        return 0;

    /* check that the mask passed in has at least one bit set in the active mask */
    cpu_mask_t active = mp_get_active_mask();
    mask &= active;
    if (unlikely(mask == 0))
        return 0;

    /* compute the highest cpu in the mask */
    cpu_num_t highest_cpu = highest_cpu_set(mask);

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
static cpu_mask_t find_cpu_mask(thread_t* t) {
    /* get the last cpu the thread ran on */
    cpu_mask_t last_ran_cpu_mask = cpu_num_to_mask(t->last_cpu);

    /* the current cpu */
    cpu_mask_t curr_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());

    /* the thread's affinity mask */
    cpu_mask_t cpu_affinity = t->cpu_affinity;

    LTRACEF_LEVEL(2, "last %#x curr %#x aff %#x name %s\n",
                  last_ran_cpu_mask, curr_cpu_mask, cpu_affinity, t->name);

    /* get a list of idle cpus and mask off the ones that aren't in our affinity mask */
    cpu_mask_t idle_cpu_mask = mp_get_idle_mask();
    cpu_mask_t active_cpu_mask = mp_get_active_mask();
    idle_cpu_mask &= cpu_affinity;
    if (idle_cpu_mask != 0) {
        if (idle_cpu_mask & curr_cpu_mask) {
            /* the current cpu is idle and within our affinity mask, so run it here */
            return curr_cpu_mask;
        }

        if (last_ran_cpu_mask & idle_cpu_mask) {
            DEBUG_ASSERT(last_ran_cpu_mask & mp_get_active_mask());
            /* the last core it ran on is idle and isn't the current cpu */
            return last_ran_cpu_mask;
        }

        /* pick an idle_cpu */
        DEBUG_ASSERT((idle_cpu_mask & mp_get_active_mask()) == idle_cpu_mask);
        return rand_cpu(idle_cpu_mask);
    }

    /* no idle cpus in our affinity mask */

    /* if the last cpu it ran on is in the affinity mask and not the current cpu, pick that */
    if ((last_ran_cpu_mask & cpu_affinity & active_cpu_mask) &&
        last_ran_cpu_mask != curr_cpu_mask) {
        return last_ran_cpu_mask;
    }

    /* fall back to picking a cpu out of the affinity mask, preferring something other
     * than the local cpu.
     * the affinity mask hard pins the thread to the cpus in the mask, so it's not possible
     * to pick a cpu outside of that list.
     */
    cpu_mask_t mask = cpu_affinity & ~(curr_cpu_mask);
    if (mask == 0)
        return curr_cpu_mask; /* local cpu is the only choice */

    mask = rand_cpu(mask);
    if (mask == 0)
        return curr_cpu_mask; /* local cpu is the only choice */
    DEBUG_ASSERT((mask & mp_get_active_mask()) == mask);
    return mask;
}

/* run queue manipulation */
static void insert_in_run_queue_head(cpu_num_t cpu, thread_t* t) {
    DEBUG_ASSERT(!list_in_list(&t->queue_node));

    list_add_head(&percpu[cpu].run_queue[t->effec_priority], &t->queue_node);
    percpu[cpu].run_queue_bitmap |= (1u << t->effec_priority);

    /* mark the cpu as busy since the run queue now has at least one item in it */
    mp_set_cpu_busy(cpu);
}

static void insert_in_run_queue_tail(cpu_num_t cpu, thread_t* t) {
    DEBUG_ASSERT(!list_in_list(&t->queue_node));

    list_add_tail(&percpu[cpu].run_queue[t->effec_priority], &t->queue_node);
    percpu[cpu].run_queue_bitmap |= (1u << t->effec_priority);

    /* mark the cpu as busy since the run queue now has at least one item in it */
    mp_set_cpu_busy(cpu);
}

/* remove the thread from the run queue it's in */
static void remove_from_run_queue(thread_t* t, int prio_queue) {
    DEBUG_ASSERT(t->state == THREAD_READY);
    DEBUG_ASSERT(is_valid_cpu_num(t->curr_cpu));

    list_delete(&t->queue_node);

    // clear the old cpu's queue bitmap if that was the last entry
    struct percpu* c = &percpu[t->curr_cpu];
    if (list_is_empty(&c->run_queue[prio_queue])) {
        c->run_queue_bitmap &= ~(1u << prio_queue);
    }
}

/* using the per cpu run queue bitmap, find the highest populated queue */
static uint highest_run_queue(const struct percpu* c) {
    return HIGHEST_PRIORITY - __builtin_clz(c->run_queue_bitmap) -
           (sizeof(c->run_queue_bitmap) * CHAR_BIT - NUM_PRIORITIES);
}

static thread_t* sched_get_top_thread(cpu_num_t cpu) {
    /* pop the head of the highest priority queue with any threads
     * queued up on the passed in cpu.
     */
    struct percpu* c = &percpu[cpu];
    if (likely(c->run_queue_bitmap)) {
        uint highest_queue = highest_run_queue(c);

        thread_t* newthread = list_remove_head_type(&c->run_queue[highest_queue], thread_t, queue_node);

        DEBUG_ASSERT(newthread);
        DEBUG_ASSERT_MSG(newthread->cpu_affinity & cpu_num_to_mask(cpu),
                         "thread %p name %s, aff %#x cpu %u\n", newthread, newthread->name,
                         newthread->cpu_affinity, cpu);
        DEBUG_ASSERT(newthread->curr_cpu == cpu);

        if (list_is_empty(&c->run_queue[highest_queue]))
            c->run_queue_bitmap &= ~(1u << highest_queue);

        LOCAL_KTRACE2("sched_get_top", newthread->priority_boost, newthread->base_priority);

        return newthread;
    }

    /* no threads to run, select the idle thread for this cpu */
    return &c->idle_thread;
}

void sched_init_thread(thread_t* t, int priority) {
    t->base_priority = priority;
    t->priority_boost = 0;
    t->inherited_priority = -1;
    compute_effec_priority(t);
}

void sched_block(void) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    __UNUSED thread_t* current_thread = get_current_thread();

    DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(current_thread->state != THREAD_RUNNING);

    LOCAL_KTRACE0("sched_block");

    /* we are blocking on something. the blocking code should have already stuck us on a queue */
    sched_resched_internal();
}

/* find a cpu to run the thread on, put it in the run queue for that cpu, and accumulate a list
 * of cpus we'll need to reschedule, including the local cpu.
 */
static void find_cpu_and_insert(thread_t* t, bool* local_resched, cpu_mask_t* accum_cpu_mask) {
    /* find a core to run it on */
    cpu_mask_t cpu = find_cpu_mask(t);
    cpu_num_t cpu_num;

    DEBUG_ASSERT(cpu != 0);

    cpu_num = lowest_cpu_set(cpu);
    if (cpu_num == arch_curr_cpu_num()) {
        *local_resched = true;
    } else {
        *accum_cpu_mask |= cpu_num_to_mask(cpu_num);
    }

    t->curr_cpu = cpu_num;
    if (t->remaining_time_slice > 0) {
        insert_in_run_queue_head(cpu_num, t);
    } else {
        insert_in_run_queue_tail(cpu_num, t);
    }
}

bool sched_unblock(thread_t* t) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    DEBUG_ASSERT(t->magic == THREAD_MAGIC);

    LOCAL_KTRACE0("sched_unblock");

    /* thread is being woken up, boost its priority */
    boost_thread(t);

    /* stuff the new thread in the run queue */
    t->state = THREAD_READY;

    bool local_resched = false;
    cpu_mask_t mask = 0;
    find_cpu_and_insert(t, &local_resched, &mask);

    if (mask)
        mp_reschedule(mask, 0);
    return local_resched;
}

bool sched_unblock_list(struct list_node* list) {
    DEBUG_ASSERT(list);
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    LOCAL_KTRACE0("sched_unblock_list");

    /* pop the list of threads and shove into the scheduler */
    bool local_resched = false;
    cpu_mask_t accum_cpu_mask = 0;
    thread_t* t;
    while ((t = list_remove_tail_type(list, thread_t, queue_node))) {
        DEBUG_ASSERT(t->magic == THREAD_MAGIC);
        DEBUG_ASSERT(!thread_is_idle(t));

        /* thread is being woken up, boost its priority */
        boost_thread(t);

        /* stuff the new thread in the run queue */
        t->state = THREAD_READY;
        find_cpu_and_insert(t, &local_resched, &accum_cpu_mask);
    }

    if (accum_cpu_mask)
        mp_reschedule(accum_cpu_mask, 0);

    return local_resched;
}

/* handle the special case of resuming a newly created idle thread */
void sched_unblock_idle(thread_t* t) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    DEBUG_ASSERT(thread_is_idle(t));
    DEBUG_ASSERT(t->cpu_affinity && (t->cpu_affinity & (t->cpu_affinity - 1)) == 0);

    /* idle thread is special case, just jam it into the cpu's run queue in the thread's
     * affinity mask and mark it ready.
     */
    t->state = THREAD_READY;
    cpu_num_t cpu = lowest_cpu_set(t->cpu_affinity);
    t->curr_cpu = cpu;
    insert_in_run_queue_head(cpu, t);
}

/* the thread is voluntarily giving up its time slice */
void sched_yield(void) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    thread_t* current_thread = get_current_thread();
    DEBUG_ASSERT(!thread_is_idle(current_thread));

    LOCAL_KTRACE0("sched_yield");

    /* consume the rest of the time slice, deboost ourself, and go to the end of a queue */
    current_thread->remaining_time_slice = 0;
    deboost_thread(current_thread, false);

    current_thread->state = THREAD_READY;

    if (local_migrate_if_needed(current_thread))
        return;

    insert_in_run_queue_tail(arch_curr_cpu_num(), current_thread);
    sched_resched_internal();
}

/* the current thread is being preempted from interrupt context */
void sched_preempt(void) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    thread_t* current_thread = get_current_thread();
    uint curr_cpu = arch_curr_cpu_num();

    DEBUG_ASSERT(current_thread->curr_cpu == curr_cpu);
    DEBUG_ASSERT(current_thread->last_cpu == current_thread->curr_cpu);
    LOCAL_KTRACE0("sched_preempt");

    current_thread->state = THREAD_READY;

    /* idle thread doesn't go in the run queue */
    if (likely(!thread_is_idle(current_thread))) {
        if (current_thread->remaining_time_slice <= 0) {
            /* if we're out of quantum, deboost the thread and put it at the tail of a queue */
            deboost_thread(current_thread, true);
        }

        if (local_migrate_if_needed(current_thread))
            return;

        if (current_thread->remaining_time_slice > 0) {
            insert_in_run_queue_head(curr_cpu, current_thread);
        } else {
            insert_in_run_queue_tail(curr_cpu, current_thread);
        }
    }

    sched_resched_internal();
}

/* the current thread is voluntarily reevaluating the scheduler on the current cpu */
void sched_reschedule(void) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    thread_t* current_thread = get_current_thread();
    uint curr_cpu = arch_curr_cpu_num();

    if (current_thread->disable_counts != 0) {
        current_thread->preempt_pending = true;
        return;
    }

    DEBUG_ASSERT(current_thread->curr_cpu == curr_cpu);
    DEBUG_ASSERT(current_thread->last_cpu == current_thread->curr_cpu);
    LOCAL_KTRACE0("sched_reschedule");

    current_thread->state = THREAD_READY;

    /* idle thread doesn't go in the run queue */
    if (likely(!thread_is_idle(current_thread))) {

        /* deboost the current thread */
        deboost_thread(current_thread, false);

        if (local_migrate_if_needed(current_thread))
            return;

        if (current_thread->remaining_time_slice > 0) {
            insert_in_run_queue_head(curr_cpu, current_thread);
        } else {
            insert_in_run_queue_tail(curr_cpu, current_thread);
        }
    }

    sched_resched_internal();
}

/* migrate the current thread to a new cpu and locally reschedule to seal the deal */
static void migrate_current_thread(thread_t* current_thread) {
    bool local_resched = false;
    cpu_mask_t accum_cpu_mask = 0;

    // current thread, so just shove ourself into another cpu's queue and reschedule locally
    current_thread->state = THREAD_READY;
    find_cpu_and_insert(current_thread, &local_resched, &accum_cpu_mask);
    if (accum_cpu_mask)
        mp_reschedule(accum_cpu_mask, 0);
    sched_resched_internal();
}

/* migrate all non-pinned threads assigned to |old_cpu| to other queues */
void sched_transition_off_cpu(cpu_num_t old_cpu) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    // Ensure we do not get scheduled on anymore.
    mp_set_curr_cpu_active(false);

    thread_t* t;
    bool local_resched = false;
    cpu_mask_t accum_cpu_mask = 0;
    cpu_mask_t pinned_mask = cpu_num_to_mask(old_cpu);
    list_node_t pinned_threads = LIST_INITIAL_VALUE(pinned_threads);
    while (!thread_is_idle(t = sched_get_top_thread(old_cpu))) {
        // Threads pinned to old_cpu can't run anywhere else, so put them
        // into a temporary list and deal with them later.
        if (t->cpu_affinity != pinned_mask) {
            find_cpu_and_insert(t, &local_resched, &accum_cpu_mask);
            DEBUG_ASSERT(!local_resched);
        } else {
            DEBUG_ASSERT(!list_in_list(&t->queue_node));
            list_add_head(&pinned_threads, &t->queue_node);
        }
    }

    // Put pinned threads back on old_cpu's queue.
    while ((t = list_remove_head_type(&pinned_threads, thread_t, queue_node)) != NULL) {
        insert_in_run_queue_head(old_cpu, t);
    }

    if (accum_cpu_mask) {
        mp_reschedule(accum_cpu_mask, 0);
    }
}

/* check to see if the current thread needs to migrate to a new core */
/* the passed argument must be the current thread and must already be pushed into the READY state */
static bool local_migrate_if_needed(thread_t* curr_thread) {
    DEBUG_ASSERT(curr_thread == get_current_thread());
    DEBUG_ASSERT(curr_thread->state == THREAD_READY);

    /* if the affinity mask does not include the current cpu, migrate us right now */
    if (unlikely((curr_thread->cpu_affinity & cpu_num_to_mask(curr_thread->curr_cpu)) == 0)) {
        migrate_current_thread(curr_thread);
        return true;
    }
    return false;
}

/* potentially migrate a thread to a new core based on the affinity mask on the thread. If it's
 * running or in a scheduler queue, handle it.
 */
void sched_migrate(thread_t* t) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    bool local_resched = false;
    cpu_mask_t accum_cpu_mask = 0;
    switch (t->state) {
    case THREAD_RUNNING:
        // see if we need to migrate
        if (t->cpu_affinity & cpu_num_to_mask(t->curr_cpu)) {
            // it's running and the new mask contains the core it's already running on, nothing to do.
            //TRACEF("t %p nomigrate\n", t);
            return;
        }

        // we need to migrate
        if (t == get_current_thread()) {
            // current thread, so just shove ourself into another cpu's queue and reschedule locally
            migrate_current_thread(t);
            return;
        } else {
            // running on another cpu, interrupt and let sched_preempt() sort it out
            accum_cpu_mask = cpu_num_to_mask(t->curr_cpu);
        }
        break;
    case THREAD_READY:
        if (t->cpu_affinity & cpu_num_to_mask(t->curr_cpu)) {
            // it's ready and the new mask contains the core it's already waiting on, nothing to do.
            //TRACEF("t %p nomigrate\n", t);
            return;
        }

        // it's sitting in a run queue somewhere, so pull it out of that one and find a new home
        DEBUG_ASSERT_MSG(list_in_list(&t->queue_node), "thread %p name %s curr_cpu %u\n", t, t->name, t->curr_cpu);
        remove_from_run_queue(t, t->effec_priority);

        find_cpu_and_insert(t, &local_resched, &accum_cpu_mask);
        break;
    default:
        // the other states do not matter, exit
        return;
    }

    // send some ipis based on the previous code
    if (accum_cpu_mask) {
        mp_reschedule(accum_cpu_mask, 0);
    }
    if (local_resched) {
        sched_reschedule();
    }
}

// the effective priority of a thread has changed, do what is necessary to move the thread
// from different queues and inform us if we need to reschedule
static void sched_priority_changed(thread_t* t, int old_prio,
                                   bool* local_resched, cpu_mask_t* accum_cpu_mask) {
    switch (t->state) {
    case THREAD_RUNNING:
        if (t->effec_priority < old_prio) {
            // we're currently running and dropped our effective priority, might want to resched
            if (t == get_current_thread()) {
                *local_resched = true;
            } else {
                *accum_cpu_mask |= cpu_num_to_mask(t->curr_cpu);
            }
        }
        break;
    case THREAD_READY:
        // it's sitting in a run queue somewhere, remove and add back to the proper queue on that cpu
        DEBUG_ASSERT_MSG(list_in_list(&t->queue_node), "thread %p name %s curr_cpu %u\n", t, t->name, t->curr_cpu);
        remove_from_run_queue(t, old_prio);

        // insert ourself into the new queue
        if (t->effec_priority > old_prio) {
            insert_in_run_queue_head(t->curr_cpu, t);

            // we may now be higher priority than the current thread on this cpu, reschedule
            if (t->curr_cpu == arch_curr_cpu_num()) {
                *local_resched = true;
            } else {
                *accum_cpu_mask |= cpu_num_to_mask(t->curr_cpu);
            }
        } else {
            insert_in_run_queue_tail(t->curr_cpu, t);
        }

        break;
    case THREAD_BLOCKED:
        // it's blocked on something, sitting in a wait queue, so we may need to move it around
        // within the wait queue.
        // note it's possible to be blocked but not in a wait queue if the thread is in transition
        // from blocked to running
        if (t->blocking_wait_queue) {
            wait_queue_priority_changed(t, old_prio);
        }
        break;
    default:
        // the other states do not matter, exit
        return;
    }
}

/* set the priority to the higher value of what it was before and the newly inherited value */
/* pri < 0 disables priority inheritance and goes back to the naturally computed values */
void sched_inherit_priority(thread_t* t, int pri, bool* local_resched) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    if (pri > HIGHEST_PRIORITY)
        pri = HIGHEST_PRIORITY;

    // if we're setting it to something real and it's less than the current, skip
    if (pri >= 0 && pri <= t->inherited_priority)
        return;

    // adjust the priority and remember the old value
    t->inherited_priority = pri;
    int old_ep = t->effec_priority;
    compute_effec_priority(t);
    if (old_ep == t->effec_priority) {
        // same effective priority, nothing to do
        return;
    }

    // see if we need to do something based on the state of the thread
    cpu_mask_t accum_cpu_mask = 0;
    sched_priority_changed(t, old_ep, local_resched, &accum_cpu_mask);

    // send some ipis based on the previous code
    if (accum_cpu_mask) {
        mp_reschedule(accum_cpu_mask, 0);
    }
}

/* preemption timer that is set whenever a thread is scheduled */
static void sched_timer_tick(timer_t* t, zx_time_t now, void* arg) {
    /* if the preemption timer went off on the idle or a real time thread, ignore it */
    thread_t* current_thread = get_current_thread();
    if (unlikely(thread_is_real_time_or_idle(current_thread)))
        return;

    LOCAL_KTRACE2("timer_tick", (uint32_t)current_thread->user_tid, current_thread->remaining_time_slice);

    /* did this tick complete the time slice? */
    DEBUG_ASSERT(now > current_thread->last_started_running);
    zx_time_t delta = now - current_thread->last_started_running;
    if (delta >= current_thread->remaining_time_slice) {
        /* we completed the time slice, do not restart it and let the scheduler run */
        current_thread->remaining_time_slice = 0;

        /* set a timer to go off on the time slice interval from now */
        timer_set_oneshot(t, now + THREAD_INITIAL_TIME_SLICE, sched_timer_tick, NULL);

        /* Mark a reschedule as pending.  The irq handler will call back
         * into us with sched_preempt(). */
        thread_preempt_set_pending();
    } else {
        /* the timer tick must have fired early, reschedule and continue */
        timer_set_oneshot(t, current_thread->last_started_running + current_thread->remaining_time_slice,
                          sched_timer_tick, NULL);
    }
}

// On ARM64 with safe-stack, it's no longer possible to use the unsafe-sp
// after set_current_thread (we'd now see newthread's unsafe-sp instead!).
// Hence this function and everything it calls between this point and the
// the low-level context switch must be marked with __NO_SAFESTACK.
__NO_SAFESTACK static void final_context_switch(thread_t* oldthread,
                                                thread_t* newthread) {
    set_current_thread(newthread);
    arch_context_switch(oldthread, newthread);
}

/**
 * @brief  Cause another thread to be executed.
 *
 * Internal reschedule routine. The current thread needs to already be in whatever
 * state and queues it needs to be in. This routine simply picks the next thread and
 * switches to it.
 */
void sched_resched_internal(void) {
    thread_t* current_thread = get_current_thread();
    uint cpu = arch_curr_cpu_num();

    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    DEBUG_ASSERT_MSG(current_thread->state != THREAD_RUNNING, "state %u\n", current_thread->state);
    DEBUG_ASSERT(!arch_in_int_handler());

    CPU_STATS_INC(reschedules);

    /* pick a new thread to run */
    thread_t* newthread = sched_get_top_thread(cpu);

    DEBUG_ASSERT(newthread);

    newthread->state = THREAD_RUNNING;

    thread_t* oldthread = current_thread;
    oldthread->preempt_pending = false;

    LOCAL_KTRACE2("resched old pri", (uint32_t)oldthread->user_tid, effec_priority(oldthread));
    LOCAL_KTRACE2("resched new pri", (uint32_t)newthread->user_tid, effec_priority(newthread));

    /* call this even if we're not changing threads, to handle the case where another
     * core rescheduled us but the work disappeared before we got to run. */
    mp_prepare_current_cpu_idle_state(thread_is_idle(newthread));

    /* if it's the same thread as we're already running, exit */
    if (newthread == oldthread)
        return;

    zx_time_t now = current_time();

    /* account for time used on the old thread */
    DEBUG_ASSERT(now >= oldthread->last_started_running);
    zx_duration_t old_runtime = now - oldthread->last_started_running;
    oldthread->runtime_ns += old_runtime;
    oldthread->remaining_time_slice -= MIN(old_runtime, oldthread->remaining_time_slice);

    /* set up quantum for the new thread if it was consumed */
    if (newthread->remaining_time_slice == 0) {
        newthread->remaining_time_slice = THREAD_INITIAL_TIME_SLICE;
    }

    newthread->last_started_running = now;

    /* mark the cpu ownership of the threads */
    if (oldthread->state != THREAD_READY)
        oldthread->curr_cpu = INVALID_CPU;
    newthread->last_cpu = cpu;
    newthread->curr_cpu = cpu;

    /* if we selected the idle thread the cpu's run queue must be empty, so mark the
     * cpu as idle */
    if (thread_is_idle(newthread)) {
        mp_set_cpu_idle(cpu);
    }

    if (thread_is_realtime(newthread)) {
        mp_set_cpu_realtime(cpu);
    } else {
        mp_set_cpu_non_realtime(cpu);
    }

    CPU_STATS_INC(context_switches);

    if (thread_is_idle(oldthread)) {
        percpu[cpu].stats.idle_time += now - oldthread->last_started_running;
    }

    LOCAL_KTRACE2("CS timeslice old", (uint32_t)oldthread->user_tid, oldthread->remaining_time_slice);
    LOCAL_KTRACE2("CS timeslice new", (uint32_t)newthread->user_tid, newthread->remaining_time_slice);

    ktrace(TAG_CONTEXT_SWITCH, (uint32_t)newthread->user_tid, cpu | (oldthread->state << 16),
           (uint32_t)(uintptr_t)oldthread, (uint32_t)(uintptr_t)newthread);

    if (thread_is_real_time_or_idle(newthread)) {
        if (!thread_is_real_time_or_idle(oldthread)) {
            /* if we're switching from a non real time to a real time, cancel
             * the preemption timer. */
            TRACE_CONTEXT_SWITCH("stop preempt, cpu %u, old %p (%s), new %p (%s)\n",
                                 cpu, oldthread, oldthread->name, newthread, newthread->name);
            timer_cancel(&percpu[cpu].preempt_timer);
        }
    } else {
        /* set up a one shot timer to handle the remaining time slice on this thread */
        TRACE_CONTEXT_SWITCH("start preempt, cpu %u, old %p (%s), new %p (%s)\n",
                             cpu, oldthread, oldthread->name, newthread, newthread->name);

        /* make sure the time slice is reasonable */
        DEBUG_ASSERT(newthread->remaining_time_slice > 0 && newthread->remaining_time_slice < ZX_SEC(1));

        /* use a special version of the timer set api that lets it reset an existing timer efficiently, given
         * that we cannot possibly race with our own timer because interrupts are disabled.
         */
        timer_reset_oneshot_local(&percpu[cpu].preempt_timer, now + newthread->remaining_time_slice, sched_timer_tick, NULL);
    }

    /* set some optional target debug leds */
    target_set_debug_led(0, !thread_is_idle(newthread));

    TRACE_CONTEXT_SWITCH("cpu %u old %p (%s, pri %d [%d:%d], flags 0x%x) "
                         "new %p (%s, pri %d [%d:%d], flags 0x%x)\n",
                         cpu, oldthread, oldthread->name, oldthread->effec_priority,
                         oldthread->base_priority, oldthread->priority_boost,
                         oldthread->flags, newthread, newthread->name,
                         newthread->effec_priority, newthread->base_priority,
                         newthread->priority_boost, newthread->flags);

    /* check that the old thread has not blown its stack just before pushing its context */
    if (THREAD_STACK_BOUNDS_CHECK &&
        unlikely(oldthread->flags & THREAD_FLAG_DEBUG_STACK_BOUNDS_CHECK)) {
        static_assert((THREAD_STACK_PADDING_SIZE % sizeof(uint32_t)) == 0, "");
        uint32_t* s = (uint32_t*)oldthread->stack;
        for (size_t i = 0; i < THREAD_STACK_PADDING_SIZE / sizeof(uint32_t); i++) {
            if (unlikely(s[i] != STACK_DEBUG_WORD)) {
                /* NOTE: will probably blow the stack harder here, but hopefully enough
                 * state exists to at least get some sort of debugging done.
                 */
                panic("stack overrun at %p: thread %p (%s), stack %p\n", &s[i],
                      oldthread, oldthread->name, oldthread->stack);
            }
        }
#if __has_feature(safe_stack)
        s = (uint32_t*)oldthread->unsafe_stack;
        for (size_t i = 0; i < THREAD_STACK_PADDING_SIZE / sizeof(uint32_t); i++) {
            if (unlikely(s[i] != STACK_DEBUG_WORD)) {
                panic("unsafe_stack overrun at %p: thread %p (%s), unsafe_stack %p\n", &s[i],
                      oldthread, oldthread->name, oldthread->unsafe_stack);
            }
        }
#endif
    }

    /* see if we need to swap mmu context */
    if (newthread->aspace != oldthread->aspace) {
        vmm_context_switch(oldthread->aspace, newthread->aspace);
    }

    /* do the low level context switch */
    final_context_switch(oldthread, newthread);
}

void sched_init_early(void) {
    /* initialize the run queues */
    for (unsigned int cpu = 0; cpu < SMP_MAX_CPUS; cpu++)
        for (unsigned int i = 0; i < NUM_PRIORITIES; i++)
            list_initialize(&percpu[cpu].run_queue[i]);
}
