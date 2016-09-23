// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
// Copyright (c) 2012-2012 Shantanu Gupta
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


/**
 * @file
 * @brief  Mutex functions
 *
 * @defgroup mutex Mutex
 * @{
 */

#include <kernel/mutex.h>
#include <debug.h>
#include <assert.h>
#include <err.h>
#include <kernel/thread.h>

/**
 * @brief  Initialize a mutex_t
 */
void mutex_init(mutex_t *m)
{
    *m = (mutex_t)MUTEX_INITIAL_VALUE(*m);
}

/**
 * @brief  Destroy a mutex_t
 *
 * This function frees any resources that were allocated
 * in mutex_init().  The mutex_t object itself is not freed.
 */
void mutex_destroy(mutex_t *m)
{
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);

#if LK_DEBUGLEVEL > 0
    if (unlikely(m->holder != 0 && get_current_thread() != m->holder))
        panic("mutex_destroy: thread %p (%s) tried to release mutex %p it doesn't own. owned by %p (%s)\n",
              get_current_thread(), get_current_thread()->name, m, m->holder, m->holder->name);
#endif

    THREAD_LOCK(state);
#if LK_DEBUGLEVEL > 0
    if (unlikely(m->count > 1 || (m->count == 1 && get_current_thread() != m->holder)))
        panic("mutex_destroy: thread %p (%s) tried to destroy mutex %p\n",
              get_current_thread(), get_current_thread()->name, m);
#endif
    m->magic = 0;
    m->count = 0;
    wait_queue_destroy(&m->wait, true);
    THREAD_UNLOCK(state);
}

status_t mutex_acquire_timeout_internal(mutex_t *m, lk_time_t timeout)
{
    if (unlikely(++m->count > 1)) {
        status_t ret = wait_queue_block(&m->wait, timeout);
        if (unlikely(ret < NO_ERROR)) {
            /* if the acquisition timed out, back out the acquire and exit */
            if (likely(ret == ERR_TIMED_OUT)) {
                m->count--;
            }
            /* if there was a general error, it may have been destroyed out from
             * underneath us, so just exit (which is really an invalid state anyway)
             */
            return ret;
        }
    }

    m->holder = get_current_thread();

    return NO_ERROR;
}

/**
 * @brief  Mutex wait with timeout
 *
 * This function waits up to \a timeout ms for the mutex to become available.
 * Timeout may be zero, in which case this function returns immediately if
 * the mutex is not free.
 *
 * @return  NO_ERROR on success, ERR_TIMED_OUT on timeout,
 * other values on error
 */
status_t mutex_acquire_timeout(mutex_t *m, lk_time_t timeout)
{
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

#if LK_DEBUGLEVEL > 0
    if (unlikely(get_current_thread() == m->holder))
        panic("mutex_acquire_timeout: thread %p (%s) tried to acquire mutex %p it already owns.\n",
              get_current_thread(), get_current_thread()->name, m);
#endif

    THREAD_LOCK(state);
    status_t ret = mutex_acquire_timeout_internal(m, timeout);
    THREAD_UNLOCK(state);
    return ret;
}

void mutex_release_internal(mutex_t *m, bool reschedule)
{
    m->holder = 0;

    if (unlikely(--m->count >= 1)) {
        /* release a thread */
        wait_queue_wake_one(&m->wait, reschedule, NO_ERROR);
    }
}

/**
 * @brief  Release mutex
 */
void mutex_release(mutex_t *m)
{
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

#if LK_DEBUGLEVEL > 0
    if (unlikely(get_current_thread() != m->holder)) {
        panic("mutex_release: thread %p (%s) tried to release mutex %p it doesn't own. owned by %p (%s)\n",
              get_current_thread(), get_current_thread()->name, m, m->holder, m->holder ? m->holder->name : "none");
    }
#endif

    THREAD_LOCK(state);
    mutex_release_internal(m, true);
    THREAD_UNLOCK(state);
}

