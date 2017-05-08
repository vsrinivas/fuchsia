// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
// Copyright (c) 2012 Shantanu Gupta
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __KERNEL_MUTEX_H
#define __KERNEL_MUTEX_H

#include <magenta/atomic.h>
#include <magenta/compiler.h>
#include <magenta/thread_annotations.h>
#include <assert.h>
#include <debug.h>
#include <stdint.h>
#include <kernel/thread.h>

__BEGIN_CDECLS

#define MUTEX_MAGIC (0x6D757478)  // 'mutx'

/* Body of the mutex.
 * The val field holds either 0 or a pointer to the thread_t holding the mutex.
 * If one or more threads are blocking and queued up, MUTEX_FLAG_QUEUED is ORed in as well.
 * NOTE: MUTEX_FLAG_QUEUED is only manipulated under the THREAD_LOCK.
 */
typedef struct TA_CAP("mutex") mutex {
    uint32_t magic;
    uintptr_t val;
    wait_queue_t wait;
} mutex_t;

#define MUTEX_FLAG_QUEUED ((uintptr_t)1)

/* accessors to extract the holder pointer from the val member */
static inline uintptr_t mutex_val(const mutex_t *m) {
    static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "");
    return atomic_load_u64_relaxed((uint64_t *)&m->val);
}

static inline thread_t *mutex_holder(const mutex_t *m) {
    static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "");
    return (thread_t *)(mutex_val(m) & ~MUTEX_FLAG_QUEUED);
}

#define MUTEX_INITIAL_VALUE(m) \
{ \
    .magic = MUTEX_MAGIC, \
    .val = 0, \
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
    return (mutex_holder(m) == get_current_thread());
}

__END_CDECLS

// Include the handy C++ Mutex/AutoLock wrappers from mxtl.  Note, this include
// must come after the kernel definition of mutex_t and the prototypes for the
// mutex routines.
#include <mxtl/auto_lock.h>
#include <mxtl/mutex.h>

#endif

