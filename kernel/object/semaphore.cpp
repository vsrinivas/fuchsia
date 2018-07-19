// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/semaphore.h>

#include <err.h>
#include <kernel/thread_lock.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

Semaphore::Semaphore(int64_t initial_count) : count_(initial_count) {
}

Semaphore::~Semaphore() {
}

void Semaphore::Post() {
    // If the count is or was negative then a thread is waiting for a resource,
    // otherwise it's safe to just increase the count available with no downsides.
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    if (unlikely(++count_ <= 0))
        waitq_.WakeOne(true, ZX_OK);
}

zx_status_t Semaphore::Wait(zx_time_t deadline) {
    thread_t *current_thread = get_current_thread();

     // If there are no resources available then we need to
     // sit in the wait queue until sem_post adds some.
    zx_status_t ret = ZX_OK;

    {
        Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
        current_thread->interruptable = true;
        bool block = --count_ < 0;

        if (unlikely(block)) {
            ret = waitq_.Block(deadline);
            if (ret < ZX_OK) {
                if ((ret == ZX_ERR_TIMED_OUT) || (ret == ZX_ERR_INTERNAL_INTR_KILLED))
                    count_++;
            }
        }

        current_thread->interruptable = false;
    }

    return ret;
}
