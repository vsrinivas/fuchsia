// Copyright 2016 The Fuchsia Authors
// Copyright 2012 Christopher Anderson <chris@nullcode.org>
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <debug.h>
#include <err.h>
#include <kernel/semaphore.h>
#include <kernel/thread.h>

void sem_init(semaphore_t *sem, unsigned int value)
{
    *sem = (semaphore_t)SEMAPHORE_INITIAL_VALUE(*sem, value);
}

void sem_destroy(semaphore_t *sem)
{
    THREAD_LOCK(state);
    sem->count = 0;
    wait_queue_destroy(&sem->wait, true);
    THREAD_UNLOCK(state);
}

int sem_post(semaphore_t *sem, bool resched)
{
    int ret = 0;

    THREAD_LOCK(state);

    /*
     * If the count is or was negative then a thread is waiting for a resource, otherwise
     * it's safe to just increase the count available with no downsides
     */
    if (unlikely(++sem->count <= 0))
        ret = wait_queue_wake_one(&sem->wait, resched, NO_ERROR);

    THREAD_UNLOCK(state);

    return ret;
}

status_t sem_wait(semaphore_t *sem)
{
    status_t ret = NO_ERROR;
    THREAD_LOCK(state);

    /*
     * If there are no resources available then we need to
     * sit in the wait queue until sem_post adds some.
     */
    if (unlikely(--sem->count < 0))
        ret = wait_queue_block(&sem->wait, INFINITE_TIME);

    THREAD_UNLOCK(state);
    return ret;
}

status_t sem_trywait(semaphore_t *sem)
{
    status_t ret = NO_ERROR;
    THREAD_LOCK(state);

    if (unlikely(sem->count <= 0))
        ret = ERR_UNAVAILABLE;
    else
        sem->count--;

    THREAD_UNLOCK(state);
    return ret;
}

status_t sem_timedwait(semaphore_t *sem, lk_time_t timeout)
{
    status_t ret = NO_ERROR;
    THREAD_LOCK(state);

    if (unlikely(--sem->count < 0)) {
        ret = wait_queue_block(&sem->wait, timeout);
        if (ret < NO_ERROR) {
            if (ret == ERR_TIMED_OUT) {
                sem->count++;
            }
        }
    }

    THREAD_UNLOCK(state);
    return ret;
}
