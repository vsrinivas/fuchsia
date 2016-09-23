// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/cond.h>

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>

void cond_init(cond_t *cond)
{
    *cond = (cond_t)COND_INITIAL_VALUE(*cond);
}

void cond_destroy(cond_t *cond)
{
    DEBUG_ASSERT(cond->magic == COND_MAGIC);

    THREAD_LOCK(state);

    cond->magic = 0;
    wait_queue_destroy(&cond->wait, true);

    THREAD_UNLOCK(state);
}

status_t cond_wait_timeout(cond_t *cond, mutex_t *mutex, lk_time_t timeout)
{
    DEBUG_ASSERT(cond->magic == COND_MAGIC);
    DEBUG_ASSERT(mutex->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    THREAD_LOCK(state);

    // We specifically want reschedule=false here, otherwise the
    // combination of releasing the mutex and enqueuing the current thread
    // would not be atomic, which would mean that we could miss wakeups.
    mutex_release_internal(mutex, /* reschedule= */ false);

    status_t result = wait_queue_block(&cond->wait, timeout);

    mutex_acquire_timeout_internal(mutex, INFINITE_TIME);

    THREAD_UNLOCK(state);

    return result;
}

void cond_signal(cond_t *cond)
{
    DEBUG_ASSERT(cond->magic == COND_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    THREAD_LOCK(state);

    wait_queue_wake_one(&cond->wait, true, NO_ERROR);

    THREAD_UNLOCK(state);
}

void cond_broadcast(cond_t *cond)
{
    DEBUG_ASSERT(cond->magic == COND_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    THREAD_LOCK(state);

    wait_queue_wake_all(&cond->wait, true, NO_ERROR);

    THREAD_UNLOCK(state);
}
