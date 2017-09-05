// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <err.h>
#include <inttypes.h>
#include <malloc.h>
#include <platform.h>
#include <stdio.h>

#include <kernel/event.h>
#include <kernel/thread.h>
#include <kernel/timer.h>

static enum handler_return timer_cb(struct timer* timer, lk_time_t now, void* arg) {
    event_t* event = (event_t*)arg;
    event_signal(event, false);

    return INT_RESCHEDULE;
}

static int timer_do_one_thread(void* arg) {
    event_t event;
    timer_t timer;

    event_init(&event, false, 0);
    timer_init(&timer);

    timer_set(&timer, current_time() + LK_MSEC(10), TIMER_SLACK_CENTER, 0, timer_cb, &event);
    event_wait(&event);

    printf("got timer on cpu %u\n", arch_curr_cpu_num());

    event_destroy(&event);

    return 0;
}

static void timer_test_all_cpus(void) {
    thread_t* timer_threads[SMP_MAX_CPUS];
    uint max = arch_max_num_cpus();

    uint i;
    for (i = 0; i < max; i++) {
        char name[16];
        snprintf(name, sizeof(name), "timer %u\n", i);

        timer_threads[i] = thread_create_etc(
            NULL, name, timer_do_one_thread, NULL,
            DEFAULT_PRIORITY, NULL, NULL, DEFAULT_STACK_SIZE, NULL);
        if (timer_threads[i] == NULL) {
            printf("failed to create thread for cpu %d\n", i);
            return;
        }
        thread_set_pinned_cpu(timer_threads[i], i);
        thread_resume(timer_threads[i]);
    }
    uint joined = 0;
    for (i = 0; i < max; i++) {
        if (thread_join(timer_threads[i], NULL, LK_SEC(1)) == 0) {
            joined += 1;
        }
    }
    printf("%u threads created, %u threads joined\n", max, joined);
}

static enum handler_return timer_cb2(struct timer* timer, lk_time_t now, void* arg) {
    int* timer_count = (int*)arg;
    atomic_add(timer_count, 1);
    return INT_RESCHEDULE;
}

static void timer_test_coalescing(enum slack_mode mode, uint64_t slack,
                                  const lk_time_t* deadline, const int64_t* expected_adj, int count) {
    printf("testing coalsecing mode %d\n", mode);

    int timer_count = 0;

    timer_t* timer = (timer_t*)malloc(sizeof(timer_t) * count);

    printf("       orig         new       adjustment\n");
    for (int ix = 0; ix != count; ++ix) {
        timer_init(&timer[ix]);
        lk_time_t dl = deadline[ix];
        timer_set(&timer[ix], dl, mode, slack, timer_cb2, &timer_count);
        printf("[%d] %" PRIu64 "  -> %" PRIu64 ", %" PRIi64 "\n",
               ix, dl, timer[ix].scheduled_time, timer[ix].slack);

        if (timer[ix].slack != expected_adj[ix]) {
            printf("\n!! unexpected adjustment! expected %" PRIi64 "\n", expected_adj[ix]);
        }
    }

    // Wait for the timers to fire.
    while (atomic_load(&timer_count) != count) {
        thread_sleep(current_time() + LK_MSEC(5));
    }

    free(timer);
}

static void timer_test_coalescing_center(void) {
    lk_time_t when = current_time() + LK_MSEC(1);
    lk_time_t off = LK_USEC(10);
    lk_time_t slack = 2u * off;

    const lk_time_t deadline[] = {
        when + (6u * off), // non-coalesced, adjustment = 0
        when,              // non-coalesced, adjustment = 0
        when - off,        // coalesced with [1], adjustment = 10u
        when - (3u * off), // non-coalesced, adjustment = 0
        when + off,        // coalesced with [1], adjustment = -10u
        when + (3u * off), // non-coalesced, adjustment = 0
        when + (5u * off), // coalesced with [0], adjustment = 10u
        when - (3u * off), // non-coalesced, same as [3], adjustment = 0
    };

    const int64_t expected_adj[countof(deadline)] = {
        0, 0, LK_USEC(10), 0, -(int64_t)LK_USEC(10), 0, LK_USEC(10), 0};

    timer_test_coalescing(
        TIMER_SLACK_CENTER, slack, deadline, expected_adj, countof(deadline));
}

static void timer_test_coalescing_late(void) {
    lk_time_t when = current_time() + LK_MSEC(1);
    lk_time_t off = LK_USEC(10);
    lk_time_t slack = 3u * off;

    const lk_time_t deadline[] = {
        when + off,        // non-coalesced, adjustment = 0
        when + (2u * off), // non-coalesced, adjustment = 0
        when - off,        // coalesced with [0], adjustment = 20u
        when - (3u * off), // non-coalesced, adjustment = 0
        when + (3u * off), // non-coalesced, adjustment = 0
        when + (2u * off), // non-coalesced, same as [1]
        when - (4u * off), // coalesced with [3], adjustment = 10u
    };

    const int64_t expected_adj[countof(deadline)] = {
        0, 0, LK_USEC(20), 0, 0, 0, LK_USEC(10)};

    timer_test_coalescing(
        TIMER_SLACK_LATE, slack, deadline, expected_adj, countof(deadline));
}

static void timer_test_coalescing_early(void) {
    lk_time_t when = current_time() + LK_MSEC(1);
    lk_time_t off = LK_USEC(10);
    lk_time_t slack = 3u * off;

    const lk_time_t deadline[] = {
        when,              // non-coalesced, adjustment = 0
        when + (2u * off), // coalesced with [0], adjustment = -20u
        when - off,        // non-coalesced, adjustment = 0
        when - (3u * off), // non-coalesced, adjustment = 0
        when + (4u * off), // non-coalesced, adjustment = 0
        when + (5u * off), // coalesced with [4], adjustment = -10u
        when - (2u * off), // coalesced with [3], adjustment = -10u
    };

    const int64_t expected_adj[countof(deadline)] = {
        0, -(int64_t)LK_USEC(20), 0, 0, 0, -(int64_t)LK_USEC(10), -(int64_t)LK_USEC(10)};

    timer_test_coalescing(
        TIMER_SLACK_EARLY, slack, deadline, expected_adj, countof(deadline));
}

static void timer_far_deadline(void) {
    event_t event;
    timer_t timer;

    event_init(&event, false, 0);
    timer_init(&timer);

    timer_set(&timer, UINT64_MAX - 5, TIMER_SLACK_CENTER, 0, timer_cb, &event);
    status_t st = event_wait_deadline(&event, current_time() + LK_MSEC(100), false);
    if (st != MX_ERR_TIMED_OUT) {
        printf("error: unexpected timer fired!\n");
    } else {
        timer_cancel(&timer);
    }

    event_destroy(&event);
}

void timer_tests(void) {
    timer_test_coalescing_center();
    timer_test_coalescing_late();
    timer_test_coalescing_early();
    timer_test_all_cpus();
    timer_far_deadline();
}
