// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/defines.h>
#include <arch/ops.h>
#include <arch/thread.h>
#include <list.h>
#include <magenta/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

/* wait queue stuff */
#define WAIT_QUEUE_MAGIC (0x77616974) // 'wait'

typedef struct wait_queue {
    int magic;
    struct list_node list;
    int count;
} wait_queue_t;

#define WAIT_QUEUE_INITIAL_VALUE(q)           \
    {                                         \
        .magic = WAIT_QUEUE_MAGIC,            \
        .list = LIST_INITIAL_VALUE((q).list), \
        .count = 0                            \
    }

/* wait queue primitive */
/* NOTE: must be inside critical section when using these */
void wait_queue_init(wait_queue_t* wait);

void wait_queue_destroy(wait_queue_t*);

/*
 * block on a wait queue.
 * return status is whatever the caller of wait_queue_wake_*() specifies.
 * a deadline other than INFINITE_TIME will abort at the specified time
 * and return MX_ERR_TIMED_OUT. a deadline in the past will immediately return.
 */
status_t wait_queue_block(wait_queue_t*, lk_time_t deadline);

/*
 * release one or more threads from the wait queue.
 * reschedule = should the system reschedule if any is released.
 * wait_queue_error = what wait_queue_block() should return for the blocking thread.
 */
int wait_queue_wake_one(wait_queue_t*, bool reschedule, status_t wait_queue_error);
int wait_queue_wake_all(wait_queue_t*, bool reschedule, status_t wait_queue_error);
struct thread* wait_queue_dequeue_one(wait_queue_t* wait, status_t wait_queue_error);

/*
 * remove the thread from whatever wait queue it's in.
 * return an error if the thread is not currently blocked (or is the current thread)
 */
status_t thread_unblock_from_wait_queue(struct thread* t, status_t wait_queue_error);

/* is the wait queue currently empty */
bool wait_queue_is_empty(wait_queue_t*);

__END_CDECLS
