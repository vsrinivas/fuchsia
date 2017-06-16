// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2009 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __KERNEL_TIMER_H
#define __KERNEL_TIMER_H

#include <magenta/compiler.h>
#include <list.h>
#include <sys/types.h>
#include <kernel/spinlock.h>

__BEGIN_CDECLS

void timer_init(void);

struct timer;
typedef enum handler_return (*timer_callback)(struct timer *, lk_time_t now, void *arg);

#define TIMER_MAGIC (0x74696D72)  //'timr'

typedef struct timer {
    int magic;
    struct list_node node;

    lk_time_t scheduled_time;
    lk_time_t period;

    timer_callback callback;
    void *arg;

    volatile int active_cpu; // <0 if inactive
    volatile bool cancel;    // true if cancel is pending
} timer_t;

#define TIMER_INITIAL_VALUE(t) \
{ \
    .magic = TIMER_MAGIC, \
    .node = LIST_INITIAL_CLEARED_VALUE, \
    .scheduled_time = 0, \
    .period = 0, \
    .callback = NULL, \
    .arg = NULL, \
    .active_cpu = -1, \
    .cancel = false, \
}

/* Rules for Timers:
 * - Timer callbacks occur from interrupt context
 * - Timers may be programmed or canceled from interrupt or thread context
 * - Timers may be canceled or reprogrammed from within their callback
 * - Setting and canceling timers is not thread safe and cannot be done concurrently
 * - timer_cancel() may spin waiting for a pending timer to complete on another cpu
*/
void timer_initialize(timer_t *);
void timer_set_oneshot(timer_t *, lk_time_t deadline, timer_callback, void *arg);
void timer_set_periodic(timer_t *, lk_time_t period, timer_callback, void *arg);
bool timer_cancel(timer_t *);

void timer_transition_off_cpu(uint old_cpu);
void timer_thaw_percpu(void);

/* special helper routine to simultaneously try to acquire a spinlock and check for
 * timer cancel, which is needed in a few special cases.
 * returns MX_OK if spinlock was acquired, MX_ERR_TIMED_OUT if timer was canceled.
 */
status_t timer_trylock_or_cancel(timer_t *t, spin_lock_t *lock);

__END_CDECLS

#endif

