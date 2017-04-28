// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
// Copyright (c) 2012 Shantanu Gupta
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __KERNEL_MUTEX_H
#define __KERNEL_MUTEX_H

#include <magenta/compiler.h>
#include <magenta/thread_annotations.h>
#include <debug.h>
#include <stdint.h>
#include <kernel/thread.h>

__BEGIN_CDECLS;

#define MUTEX_MAGIC (0x6D757478)  // 'mutx'

typedef struct TA_CAP("mutex") mutex {
    uint32_t magic;
    thread_t *holder;
    int count;
    wait_queue_t wait;
} mutex_t;

#define MUTEX_INITIAL_VALUE(m) \
{ \
    .magic = MUTEX_MAGIC, \
    .holder = NULL, \
    .count = 0, \
    .wait = WAIT_QUEUE_INITIAL_VALUE((m).wait), \
}

/* Rules for Mutexes:
 * - Mutexes are only safe to use from thread context.
 * - Mutexes are non-recursive.
*/
void mutex_init(mutex_t *m);
void mutex_destroy(mutex_t *m);
void mutex_acquire(mutex_t *m) TA_ACQ(m);
void mutex_release(mutex_t *m) TA_REL(m);

/* special version of the above with the thread lock held */
void mutex_release_thread_locked(mutex_t *m, bool resched) TA_REL(m);

/* does the current thread hold the mutex? */
static inline bool is_mutex_held(const mutex_t *m)
{
    return m->holder == get_current_thread();
}

__END_CDECLS;

// Include the handy C++ Mutex/AutoLock wrappers from mxtl.  Note, this include
// must come after the kernel definition of mutex_t and the prototypes for the
// mutex routines.
#include <mxtl/auto_lock.h>
#include <mxtl/mutex.h>

#endif

