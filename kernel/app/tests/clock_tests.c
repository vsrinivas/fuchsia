// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <rand.h>
#include <err.h>
#include <inttypes.h>
#include <app/tests.h>
#include <kernel/thread.h>
#include <kernel/mutex.h>
#include <kernel/event.h>
#include <platform.h>

void clock_tests(void)
{
    uint64_t c;
    lk_bigtime_t t2;

    thread_sleep_relative(LK_MSEC(100));
    c = arch_cycle_count();
    current_time_hires();
    c = arch_cycle_count() - c;
    printf("%" PRIu64 " cycles per current_time_hires()\n", c);

    printf("making sure time never goes backwards\n");
    {
        printf("testing current_time_hires()\n");
        lk_bigtime_t start = current_time_hires();
        lk_bigtime_t last = start;
        for (;;) {
            t2 = current_time_hires();
            //printf("%llu %llu\n", last, t2);
            if (t2 < last) {
                printf("WARNING: time ran backwards: %" PRIu64 " < %" PRIu64 "\n", t2, last);
                last = t2;
                continue;
            }
            last = t2;
            if (last - start > LK_MSEC(5))
                break;
        }
    }

    printf("counting to 5, in one second intervals\n");
    for (int i = 0; i < 5; i++) {
        thread_sleep_relative(LK_SEC(1));
        printf("%d\n", i + 1);
    }

    printf("measuring cpu clock against current_time_hires()\n");
    for (int i = 0; i < 5; i++) {
        uint64_t cycles = arch_cycle_count();
        lk_bigtime_t start = current_time_hires();
        while ((current_time_hires() - start) < LK_SEC(1))
            ;
        cycles = arch_cycle_count() - cycles;
        printf("%" PRIu64 " cycles per second\n", cycles);
    }
}
