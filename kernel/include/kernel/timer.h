// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2009 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/spinlock.h>
#include <list.h>
#include <magenta/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

void timer_queue_init(void);

struct timer;
typedef enum handler_return (*timer_callback)(struct timer*, lk_time_t now, void* arg);

#define TIMER_MAGIC (0x74696D72) //'timr'

enum slack_mode {
    TIMER_SLACK_CENTER, // slack is centered arround dealine
    TIMER_SLACK_LATE,   // slack interval is [deadline, dealine + slack)
    TIMER_SLACK_EARLY,  // slack interval is (deadline - slack, dealine]
};

typedef struct timer {
    int magic;
    struct list_node node;

    lk_time_t scheduled_time;
    int64_t slack; // Stores the applied slack adjustment from
                   // the ideal scheduled_time.
    timer_callback callback;
    void* arg;

    volatile int active_cpu; // <0 if inactive
    volatile bool cancel;    // true if cancel is pending
} timer_t;

#define TIMER_INITIAL_VALUE(t)              \
    {                                       \
        .magic = TIMER_MAGIC,               \
        .node = LIST_INITIAL_CLEARED_VALUE, \
        .scheduled_time = 0,                \
        .slack = 0,                         \
        .callback = NULL,                   \
        .arg = NULL,                        \
        .active_cpu = -1,                   \
        .cancel = false,                    \
    }

/* Rules for Timers:
 * - Timer callbacks occur from interrupt context
 * - Timers may be programmed or canceled from interrupt or thread context
 * - Timers may be canceled or reprogrammed from within their callback
 * - Setting and canceling timers is not thread safe and cannot be done concurrently
 * - timer_cancel() may spin waiting for a pending timer to complete on another cpu
 */

/**
 * Initialize a timer object
 */
void timer_init(timer_t*);

/**
 * Set up a timer that executes once
 *
 * This function specifies a callback function to be run after a specified
 * deadline passes. The function will be called one time.
 *
 * timer: the timer to use
 * deadline: absolute time, in ns, after which the timer is executed
 * mode: type of slack to apply, either symmetrical or one-sided to early or late
 * slack: delta time in nanoseconds from |deadline| after or before is
 *        acceptable to execute the timer.
 * callback: the function to call when the timer expires
 * arg: the argument to pass to the callback
 *
 * The timer function is declared as:
 *   enum handler_return callback(struct timer *, lk_time_t now, void *arg) { ... }
 *
 * The |slack| parameter defines an interval depending on the |mode| in which
 * is acceptable to fire the timer:
 *
 * - TIMER_SLACK_CENTER: |deadline - slack| to |deadline + slack|
 * - TIMER_SLACK_LATE: |dealine| to |deadline + slack|
 * - TIMER_SLACK_EARLY: |deadline - slack| to |deadline|
 *
 */
void timer_set(timer_t* timer, lk_time_t deadline,
               enum slack_mode mode, uint64_t slack, timer_callback callback, void* arg);

/**
 * Cancel a pending timer
 *
 * Returns true if the timer was canceled before it was
 * scheduled in a cpu and false otherwise or if the timer
 * was not scheduled at all.
 *
 */
bool timer_cancel(timer_t*);

/* Equivalent to timer_set with a slack of 0 */
static inline void timer_set_oneshot(
    timer_t* timer, lk_time_t deadline, timer_callback callback, void* arg) {
    return timer_set(timer, deadline, TIMER_SLACK_CENTER, 0ull, callback, arg);
}

/* Similar to timer_set_oneshot, with additional constraints:
 * - Will reset a currently active timer
 * - Must be called with interrupts disabled
 * - Must be running on the cpu that the timer is set to fire on (if currently set)
 * - Cannot be called from the timer itself
 */
/* NOTE: internal api that is needed probably only by the scheduler */
void timer_reset_oneshot_local(timer_t* timer, lk_time_t deadline, timer_callback callback, void* arg);

/* Internal routines used when bringing cpus online/offline */

/* Moves timers from |old_cpu| to the current cpu
 */
void timer_transition_off_cpu(uint old_cpu);

/* This function is to be invoked after resume on each CPU that may have
 * had timers still on it, in order to restart hardware timers.
 */
void timer_thaw_percpu(void);

/* Special helper routine to simultaneously try to acquire a spinlock and check for
 * timer cancel, which is needed in a few special cases.
 * returns MX_OK if spinlock was acquired, MX_ERR_TIMED_OUT if timer was canceled.
 */
status_t timer_trylock_or_cancel(timer_t* t, spin_lock_t* lock);

__END_CDECLS
