// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>
#include <kernel/thread.h>
#include <zircon/types.h>

// A basic counting semaphore. It directly uses the low-level wait queue API.
class Semaphore {
public:
    explicit Semaphore(int64_t initial_count = 0);

    Semaphore(const Semaphore&) = delete;
    Semaphore(Semaphore&&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    ~Semaphore() = default;

    // Increment the counter, possibly releasing one thread. The returned
    // value is the new counter value which is only useful for testing.
    int64_t Post();

    // Interruptable wait for the counter to be > 0 or for |deadline| to pass.
    // If the wait was satisfied by Post() the return is ZX_OK and the count is
    // decremented by one.
    // Otherwise the count is not decremented. The return value can be
    // ZX_ERR_TIMED_OUT if the deadline had passed or one of ZX_ERR_INTERNAL_INTR
    // errors if the thread had a signal delivered.
    zx_status_t Wait(const Deadline& deadline);

private:
    int64_t count_;
    WaitQueue waitq_;
};
