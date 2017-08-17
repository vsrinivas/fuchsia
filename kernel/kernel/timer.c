// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * @file
 * @brief  Kernel timer subsystem
 * @defgroup timer Timers
 *
 * The timer subsystem allows functions to be scheduled for later
 * execution.  Each timer object is used to cause one function to
 * be executed at a later time.
 *
 * Timer callback functions are called in interrupt context.
 *
 * @{
 */
#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/spinlock.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <list.h>
#include <malloc.h>
#include <platform.h>
#include <platform/timer.h>
#include <trace.h>

#define LOCAL_TRACE 0

static spin_lock_t timer_lock;

void timer_init(timer_t* timer) {
    *timer = (timer_t)TIMER_INITIAL_VALUE(*timer);
}

static void insert_timer_in_queue(uint cpu, timer_t* timer,
                                  uint64_t early_slack, uint64_t late_slack) {

    DEBUG_ASSERT(arch_ints_disabled());
    LTRACEF("timer %p, cpu %u, scheduled %" PRIu64 "\n", timer, cpu, timer->scheduled_time);

    lk_time_t earliest_deadline = timer->scheduled_time - early_slack;
    lk_time_t latest_deadline = timer->scheduled_time + late_slack;

    // For inserting the timer we consider several cases. In general we
    // want to coalesce with the current timer unless we can prove that
    // either that:
    //  1- there is no slack overlap with current timer OR
    //  2- the next timer is a better fit.
    //
    // In diagrams that follow
    // - Let |e| be the current (existing) timer deadline
    // - Let |t| be the deadline of the timer we are inserting
    // - Let |n| be the next timer deadline if any
    // - Let |x| be the end of the list (not a timer)
    // - Let |(| and |)| the earliest_deadline and latest_deadline.
    //
    timer_t* entry;

    list_for_every_entry (&percpu[cpu].timer_queue, entry, timer_t, node) {
        if (entry->scheduled_time > latest_deadline) {
            // New timer latest is earlier than the current timer.
            // Just add upfront as is, without slack.
            //
            //   ---------t---)--e-------------------------------> time
            //
            timer->slack = 0ull;
            list_add_before(&entry->node, &timer->node);
            return;
        }

        if (entry->scheduled_time >= timer->scheduled_time) {
            //  New timer slack overlaps and is to the left (or equal). We
            //  coalesce with current by scheduling late.
            //
            //  --------(----t---e-)----------------------------> time
            //
            timer->slack = entry->scheduled_time - timer->scheduled_time;
            timer->scheduled_time = entry->scheduled_time;
            list_add_after(&entry->node, &timer->node);
            return;
        }

        if (entry->scheduled_time < earliest_deadline) {
            // new timer earliest is later than the current timer. This case
            // is handled in a future iteration.
            //
            //   ----------------e--(---t-----------------------> time
            //
            continue;
        }

        // New timer is to the right of current timer and there is overlap
        // with the current timer, but could the next timer (if any) be
        // a better fit?
        //
        //  -------------(--e---t-----?-------------------> time
        //

        const timer_t* next =
            list_next_type(&percpu[cpu].timer_queue, &entry->node, timer_t, node);

        if (next != NULL) {
            if (next->scheduled_time <= timer->scheduled_time) {
                // The new timer is to the right of the next timer. There is no
                // chance the current timer is a better fit.
                //
                //  -------------(--e---n---t----------------------> time
                //
                continue;
            }

            if (next->scheduled_time < latest_deadline) {
                // There is slack overlap with the next timer, and also with the
                // current timer. Which coalescing is a better match?
                //
                //  --------------(-e---t---n-)-----------------------> time
                //
                lk_time_t delta_entry = timer->scheduled_time - entry->scheduled_time;
                lk_time_t delta_next = next->scheduled_time - timer->scheduled_time;
                if (delta_next < delta_entry) {
                    // New timer is closer to the next timer, handle it in the
                    // next iteration.
                    continue;
                }
            }
        }

        // Handles the remaining cases, note that there is overlap with
        // the current timer.
        //
        //  1- this is the last timer (next == NULL) or
        //  2- there is no overlap with the next timer, or
        //  3- there is overlap with both current and next but
        //     current is closer.
        //
        //  So we coalesce by scheduling early.
        //
        timer->slack = entry->scheduled_time - timer->scheduled_time;
        timer->scheduled_time = entry->scheduled_time;
        list_add_after(&entry->node, &timer->node);
        return;
    }

    // Walked off the end of the list and there was no overlap.
    timer->slack = 0ull;
    list_add_tail(&percpu[cpu].timer_queue, &timer->node);
}

void timer_set(timer_t* timer, lk_time_t deadline,
               enum slack_mode mode, uint64_t slack,
               timer_callback callback, void* arg) {
    LTRACEF("timer %p deadline %" PRIu64 " slack %" PRIu64 " callback %p arg %p\n",
            timer, deadline, slack, callback, arg);

    DEBUG_ASSERT(timer->magic == TIMER_MAGIC);
    DEBUG_ASSERT(mode <= TIMER_SLACK_EARLY);

    if (list_in_list(&timer->node)) {
        panic("timer %p already in list\n", timer);
    }

    lk_time_t late_slack;
    lk_time_t early_slack;

    if (slack == 0u) {
        late_slack = 0u;
        early_slack = 0u;
    } else {
        switch (mode) {
        case TIMER_SLACK_CENTER:
            late_slack = ((deadline + slack) < deadline) ? (UINT64_MAX - deadline) : slack;
            early_slack = ((deadline - slack) > deadline) ? deadline : slack;
            break;
        case TIMER_SLACK_LATE:
            late_slack = ((deadline + slack) < deadline) ? (UINT64_MAX - deadline) : slack;
            early_slack = 0u;
            break;
        case TIMER_SLACK_EARLY:
            early_slack = ((deadline - slack) > deadline) ? deadline : slack;
            late_slack = 0u;
            break;
        default:
            panic("invalid timer mode\n");
        };
    }

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&timer_lock, state);

    uint cpu = arch_curr_cpu_num();

    bool currently_active = (timer->active_cpu == (int)cpu);
    if (unlikely(currently_active)) {
        /* the timer is active on our own cpu, we must be inside the callback */
        if (timer->cancel)
            goto out;
    } else if (unlikely(timer->active_cpu >= 0)) {
        panic("timer %p currently active on a different cpu %d\n", timer, timer->active_cpu);
    }

    // Set up the structure.
    timer->scheduled_time = deadline;
    timer->callback = callback;
    timer->arg = arg;
    timer->cancel = false;
    // We don't need to modify timer->active_cpu because it is managed by timer_tick().

    LTRACEF("scheduled time %" PRIu64 "\n", timer->scheduled_time);

    insert_timer_in_queue(cpu, timer, early_slack, late_slack);

    if (list_peek_head_type(&percpu[cpu].timer_queue, timer_t, node) == timer) {
        /* we just modified the head of the timer queue */
        LTRACEF("setting new timer for %" PRIu64 " nsecs\n", deadline);
        platform_set_oneshot_timer(deadline);
    }

out:
    spin_unlock_irqrestore(&timer_lock, state);
}

/* similar to timer_set_oneshot, with additional features/constraints:
 * - will reset a currently active timer
 * - must be called with interrupts disabled
 * - must be running on the cpu that the timer is set to fire on (if currently set)
 * - cannot be called from the timer itself
 */
void timer_reset_oneshot_local(timer_t* timer, lk_time_t deadline, timer_callback callback, void* arg) {
    LTRACEF("timer %p, deadline %" PRIu64 ", callback %p, arg %p\n", timer, deadline, callback, arg);

    DEBUG_ASSERT(timer->magic == TIMER_MAGIC);
    DEBUG_ASSERT(arch_ints_disabled());

    uint cpu = arch_curr_cpu_num();

    /* no need to disable interrupts when acquiring this lock */
    spin_lock(&timer_lock);

    if (unlikely(timer->active_cpu >= 0)) {
        panic("timer %p currently active\n", timer);
    }

    /* remove it from the queue if it was present */
    if (list_in_list(&timer->node))
        list_delete(&timer->node);

    /* set up the structure */
    timer->scheduled_time = deadline;
    timer->slack = 0;
    timer->callback = callback;
    timer->arg = arg;
    timer->cancel = false;
    timer->active_cpu = -1;

    LTRACEF("scheduled time %" PRIu64 "\n", timer->scheduled_time);

    insert_timer_in_queue(cpu, timer, 0u, 0u);

    if (list_peek_head_type(&percpu[cpu].timer_queue, timer_t, node) == timer) {
        /* we just modified the head of the timer queue */
        LTRACEF("setting new timer for %" PRIu64 " nsecs\n", deadline);
        platform_set_oneshot_timer(deadline);
    }

    spin_unlock(&timer_lock);
}

bool timer_cancel(timer_t* timer) {
    DEBUG_ASSERT(timer->magic == TIMER_MAGIC);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&timer_lock, state);

    uint cpu = arch_curr_cpu_num();

    /* mark the timer as canceled */
    timer->cancel = true;
    smp_mb();

    /* wake up any spinners on the cancel signal */
    arch_spinloop_signal();

    /* see if we're trying to cancel the timer we're currently in the middle of handling */
    if (unlikely(timer->active_cpu == (int)cpu)) {
        /* zero it out */
        timer->callback = NULL;
        timer->arg = NULL;

        /* we're done, so return back to the callback */
        spin_unlock_irqrestore(&timer_lock, state);
        return false;
    }

    bool callback_not_running;

    /* if the timer is in a queue, remove it and adjust hardware timers if needed */
    if (list_in_list(&timer->node)) {
        callback_not_running = true;

        /* save a copy of the old head of the queue */
        timer_t* oldhead = list_peek_head_type(&percpu[cpu].timer_queue, timer_t, node);

        /* remove our timer from the queue */
        list_delete(&timer->node);

        /* TODO(cpu): if  after removing |timer| there is one other single timer with
           the same scheduled_time and slack non-zero then it is possible to return
           that timer to the ideal scheduled_time */

        /* see if we've just modified the head of this cpu's timer queue */
        /* if we modified another cpu's queue, we'll just let it fire and sort itself out */
        if (unlikely(oldhead == timer)) {
            timer_t* newhead = list_peek_head_type(&percpu[cpu].timer_queue, timer_t, node);
            if (newhead) {
                LTRACEF("setting new timer to %" PRIu64 "\n", newhead->scheduled_time);
                platform_set_oneshot_timer(newhead->scheduled_time);
            } else {
                LTRACEF("clearing old hw timer, nothing in the queue\n");
                platform_stop_timer();
            }
        }
    } else {
        callback_not_running = false;
    }

    spin_unlock_irqrestore(&timer_lock, state);

    /* wait for the timer to become un-busy in case a callback is currently active on another cpu */
    while (timer->active_cpu >= 0) {
        arch_spinloop_pause();
    }

    /* zero it out */
    timer->callback = NULL;
    timer->arg = NULL;

    return callback_not_running;
}

/* called at interrupt time to process any pending timers */
enum handler_return timer_tick(lk_time_t now) {
    timer_t* timer;
    enum handler_return ret = INT_NO_RESCHEDULE;

    DEBUG_ASSERT(arch_ints_disabled());

    CPU_STATS_INC(timer_ints);

    uint cpu = arch_curr_cpu_num();

    LTRACEF("cpu %u now %" PRIu64 ", sp %p\n", cpu, now, __GET_FRAME());

    spin_lock(&timer_lock);

    for (;;) {
        /* see if there's an event to process */
        timer = list_peek_head_type(&percpu[cpu].timer_queue, timer_t, node);
        if (likely(timer == 0))
            break;
        LTRACEF("next item on timer queue %p at %" PRIu64 " now %" PRIu64 " (%p, arg %p)\n",
                timer, timer->scheduled_time, now, timer->callback, timer->arg);
        if (likely(now < timer->scheduled_time))
            break;

        /* process it */
        LTRACEF("timer %p\n", timer);
        DEBUG_ASSERT_MSG(timer && timer->magic == TIMER_MAGIC,
                         "ASSERT: timer failed magic check: timer %p, magic 0x%x\n",
                         timer, (uint)timer->magic);
        list_delete(&timer->node);

        /* mark the timer busy */
        timer->active_cpu = cpu;
        /* spinlock below acts as a memory barrier */

        /* we pulled it off the list, release the list lock to handle it */
        spin_unlock(&timer_lock);

        LTRACEF("dequeued timer %p, scheduled %" PRIu64 "\n", timer, timer->scheduled_time);

        CPU_STATS_INC(timers);

        LTRACEF("timer %p firing callback %p, arg %p\n", timer, timer->callback, timer->arg);
        if (timer->callback(timer, now, timer->arg) == INT_RESCHEDULE)
            ret = INT_RESCHEDULE;

        DEBUG_ASSERT(arch_ints_disabled());
        /* it may have been requeued, grab the lock so we can safely inspect it */
        spin_lock(&timer_lock);

        /* mark it not busy */
        timer->active_cpu = -1;
        smp_mb();

        /* make sure any spinners wake up */
        arch_spinloop_signal();
    }

    /* reset the timer to the next event */
    timer = list_peek_head_type(&percpu[cpu].timer_queue, timer_t, node);
    if (timer) {
        /* has to be the case or it would have fired already */
        DEBUG_ASSERT(timer->scheduled_time > now);

        LTRACEF("setting new timer for %" PRIu64 " nsecs for event %p\n", timer->scheduled_time,
                timer);
        platform_set_oneshot_timer(timer->scheduled_time);
    }

    /* we're done manipulating the timer queue */
    spin_unlock(&timer_lock);

    return ret;
}

status_t timer_trylock_or_cancel(timer_t* t, spin_lock_t* lock) {
    /* spin trylocking on the passed in spinlock either waiting for it
     * to grab or the passed in timer to be canceled.
     */
    while (unlikely(spin_trylock(lock))) {
        /* we failed to grab it, check for cancel */
        if (t->cancel) {
            /* we were canceled, so bail immediately */
            return MX_ERR_TIMED_OUT;
        }
        /* tell the arch to wait */
        arch_spinloop_pause();
    }

    return MX_OK;
}

void timer_transition_off_cpu(uint old_cpu) {
    spin_lock_saved_state_t state;
    spin_lock_irqsave(&timer_lock, state);
    uint cpu = arch_curr_cpu_num();

    timer_t* old_head = list_peek_head_type(&percpu[old_cpu].timer_queue, timer_t, node);

    timer_t *entry = NULL, *tmp_entry = NULL;
    /* Move all timers from old_cpu to this cpu */
    list_for_every_entry_safe (&percpu[old_cpu].timer_queue, entry, tmp_entry, timer_t, node) {
        list_delete(&entry->node);
        // We lost the original asymmetric slack information so when we combine them
        // with the other timer queue they are not coalesced again.
        // TODO(cpu): figure how important this case is.
        insert_timer_in_queue(cpu, entry, 0u, 0u);
    }

    timer_t* new_head = list_peek_head_type(&percpu[cpu].timer_queue, timer_t, node);
    if (new_head != NULL && new_head != old_head) {
        /* we just modified the head of the timer queue */
        LTRACEF("setting new timer for %" PRIu64 " nsecs\n", new_head->scheduled_time);
        platform_set_oneshot_timer(new_head->scheduled_time);
    }

    spin_unlock_irqrestore(&timer_lock, state);
}

void timer_thaw_percpu(void) {
    DEBUG_ASSERT(arch_ints_disabled());
    spin_lock(&timer_lock);

    uint cpu = arch_curr_cpu_num();

    timer_t* t = list_peek_head_type(&percpu[cpu].timer_queue, timer_t, node);
    if (t) {
        LTRACEF("rescheduling timer for %" PRIu64 " nsecs\n", t->scheduled_time);
        platform_set_oneshot_timer(t->scheduled_time);
    }

    spin_unlock(&timer_lock);
}

void timer_queue_init(void) {
    timer_lock = SPIN_LOCK_INITIAL_VALUE;
    for (uint i = 0; i < SMP_MAX_CPUS; i++) {
        list_initialize(&percpu[i].timer_queue);
    }
}

// print a timer queue dump into the passed in buffer
static void dump_timer_queues(char* buf, size_t len) {
    size_t ptr = 0;
    lk_time_t now = current_time();

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&timer_lock, state);

    for (uint i = 0; i < SMP_MAX_CPUS; i++) {
        if (mp_is_cpu_online(i)) {
            ptr += snprintf(buf + ptr, len - ptr, "cpu %u:\n", i);

            timer_t* t;
            lk_time_t last = now;
            list_for_every_entry (&percpu[i].timer_queue, t, timer_t, node) {
                lk_time_t delta_now = (t->scheduled_time > now) ? (t->scheduled_time - now) : 0;
                lk_time_t delta_last = (t->scheduled_time > last) ? (t->scheduled_time - last) : 0;
                ptr += snprintf(buf + ptr, len - ptr,
                                "\ttime %" PRIu64 " delta_now %" PRIu64 " delta_last %" PRIu64 " func %p arg %p\n",
                                t->scheduled_time, delta_now, delta_last, t->callback, t->arg);
                last = t->scheduled_time;
            }
        }
    }

    spin_unlock_irqrestore(&timer_lock, state);
}

#if WITH_LIB_CONSOLE
#include <lib/console.h>

static int cmd_timers(int argc, const cmd_args* argv, uint32_t flags) {
    const size_t timer_buffer_size = PAGE_SIZE;

    // allocate a buffer to dump the timer queue into to avoid reentrancy issues with the
    // timer spinlock
    char* buf = malloc(timer_buffer_size);
    if (!buf)
        return MX_ERR_NO_MEMORY;

    dump_timer_queues(buf, timer_buffer_size);

    printf("%s", buf);

    free(buf);

    return 0;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 1
STATIC_COMMAND_MASKED("timers", "dump the current kernel timer queues", &cmd_timers, CMD_AVAIL_NORMAL)
#endif
STATIC_COMMAND_END(kernel);

#endif // WITH_LIB_CONSOLE
