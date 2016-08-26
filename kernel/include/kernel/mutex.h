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
#include <debug.h>
#include <stdint.h>
#include <kernel/thread.h>

__BEGIN_CDECLS;

#define MUTEX_MAGIC (0x6D757478)  // 'mutx'

typedef struct mutex {
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

void mutex_init(mutex_t *);
void mutex_destroy(mutex_t *);
status_t mutex_acquire_timeout(mutex_t *, lk_time_t); /* try to acquire the mutex with a timeout value */
void mutex_release(mutex_t *);

/* Internal functions for use by condvar implementation. */
status_t mutex_acquire_timeout_internal(mutex_t *m, lk_time_t timeout);
void mutex_release_internal(mutex_t *m, bool reschedule);

static inline void mutex_acquire(mutex_t *m)
{
    mutex_acquire_timeout(m, INFINITE_TIME);
}

/* does the current thread hold the mutex? */
static bool is_mutex_held(const mutex_t *m)
{
    return m->holder == get_current_thread();
}

__END_CDECLS;

#ifdef __cplusplus
class Mutex {
public:
    constexpr Mutex() : mutex_(MUTEX_INITIAL_VALUE(mutex_)) { }

    ~Mutex() {
        mutex_destroy(&mutex_);
    }

    void Acquire() {
        mutex_acquire(&mutex_);
    }

    status_t AcquireTimeout(lk_time_t timeout) {
        return mutex_acquire_timeout(&mutex_, timeout);
    }

    void Release() {
        mutex_release(&mutex_);
    }

    bool IsHeld() const {
        return is_mutex_held(&mutex_);
    }

    mutex_t* GetInternal() {
        return &mutex_;
    }

    // suppress default constructors
    Mutex(const Mutex& am) = delete;
    Mutex& operator=(const Mutex& am) = delete;
    Mutex(Mutex&& c) = delete;
    Mutex& operator=(Mutex&& c) = delete;
private:
    mutex_t mutex_;
};
#endif

#endif

