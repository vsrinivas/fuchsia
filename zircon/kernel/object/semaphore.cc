// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/semaphore.h>

#include <err.h>
#include <kernel/thread_lock.h>
#include <zircon/compiler.h>

Semaphore::Semaphore(int64_t initial_count) : count_(initial_count) {
}

int64_t Semaphore::Post() {
    // If the count is or was negative then a thread is waiting for a resource,
    // otherwise it's safe to just increase the count available with no downsides.
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    if (likely(++count_ <= 0))
        waitq_.WakeOne(true, ZX_OK);
    return count_;
}

zx_status_t Semaphore::Wait(const Deadline& deadline) {
    thread_t* current_thread = get_current_thread();

    // If there are no resources available then we need to sit in the
    // wait queue until sem_post adds some or a signal gets delivered.
    zx_status_t ret = ZX_OK;

    {
        Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
        bool block = --count_ < 0;

        if (likely(block)) {
            current_thread->interruptable = true;
            ret = waitq_.Block(deadline);
            current_thread->interruptable = false;

            if (ret != ZX_OK) {
                count_++;
            }
        }

    }

    return ret;
}
