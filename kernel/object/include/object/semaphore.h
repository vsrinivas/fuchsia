// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>
#include <kernel/thread.h>
#include <magenta/types.h>

// You probably don't want to use this class.
class Semaphore {
public:
    explicit Semaphore(int64_t initial_count = 0);
    ~Semaphore();

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    // Returns the number of ready threads. If it is bigger than 0
    // the caller must call thread_reschedule().
    __WARN_UNUSED_RESULT int Post();
    mx_status_t Wait(lk_time_t deadline);

private:
    int64_t count_;
    wait_queue_t waitq_;
};
