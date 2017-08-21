// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/semaphore.h>

#include <err.h>
#include <magenta/compiler.h>

Semaphore::Semaphore(int64_t initial_count) : count_(initial_count) {
    wait_queue_init(&waitq_);
}

Semaphore::~Semaphore() {
    THREAD_LOCK(state);
    wait_queue_destroy(&waitq_);
    THREAD_UNLOCK(state);
}

int Semaphore::Post() {
    // If the count is or was negative then a thread is waiting for a resource,
    // otherwise it's safe to just increase the count available with no downsides.
    int ret = 0;
    THREAD_LOCK(state);
    if (unlikely(++count_ <= 0))
        ret = wait_queue_wake_one(&waitq_, false, MX_OK);
    THREAD_UNLOCK(state);
    return ret;
}

mx_status_t Semaphore::Wait(lk_time_t deadline) {
    thread_t *current_thread = get_current_thread();

     // If there are no resources available then we need to
     // sit in the wait queue until sem_post adds some.
    mx_status_t ret = MX_OK;
    THREAD_LOCK(state);
    current_thread->interruptable = true;

    if (unlikely(--count_ < 0)) {
        ret = wait_queue_block(&waitq_, deadline);
        if (ret < MX_OK) {
            if ((ret == MX_ERR_TIMED_OUT) || (ret == MX_ERR_INTERNAL_INTR_KILLED))
                count_++;
        }
    }

    current_thread->interruptable = false;
    THREAD_UNLOCK(state);
    return ret;
}
