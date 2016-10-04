// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <inttypes.h>
#include <kernel/thread.h>
#include <platform.h>
#include <app/tests.h>

// Tests that thread_sleep and current_time_hires() are consistent.
static int thread_sleep_test(void)
{
    int early = 0;
    for (int i = 0; i < 5; i++) {
        lk_bigtime_t now = current_time_hires();
        thread_sleep(500);
        lk_bigtime_t actual_delay = current_time_hires() - now;
        if (actual_delay < 500 * 1000) {
            early = 1;
            printf("thread_sleep(500) returned after %" PRIu64 " ns\n", actual_delay);
        }
    }
    return early;
}

int sleep_tests(void)
{
    return thread_sleep_test();
}
