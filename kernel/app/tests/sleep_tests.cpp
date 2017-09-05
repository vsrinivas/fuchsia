// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <inttypes.h>
#include <kernel/thread.h>
#include <platform.h>
#include <stdio.h>

// Tests that thread_sleep and current_time() are consistent.
static int thread_sleep_test(void) {
    int early = 0;
    for (int i = 0; i < 5; i++) {
        lk_time_t now = current_time();
        thread_sleep_relative(LK_MSEC(500));
        lk_time_t actual_delay = current_time() - now;
        if (actual_delay < LK_MSEC(500)) {
            early = 1;
            printf("thread_sleep_relative(LK_MSEC(500)) returned after %" PRIu64 " ns\n", actual_delay);
        }
    }
    return early;
}

int sleep_tests(void) {
    return thread_sleep_test();
}
