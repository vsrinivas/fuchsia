// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

static bool wait(mx_handle_t event, mx_handle_t quit_event) {
    mx_status_t ms;
    mx_signals_t signals[2] = {MX_SIGNAL_SIGNALED, MX_SIGNAL_SIGNALED};
    mx_signals_state_t states[2] = {};
    mx_handle_t wev[2] = {event, quit_event};

    ms = mx_handle_wait_many(2U, wev, signals, MX_TIME_INFINITE, NULL, states);
    if (ms < 0)
        return false;

    return (states[1].satisfied == MX_SIGNAL_SIGNALED);
}

static bool wait_user(mx_handle_t event, mx_handle_t quit_event, mx_signals_t user_signal) {
    mx_status_t ms;
    mx_signals_t signals[2] = {user_signal, MX_SIGNAL_SIGNALED};
    mx_signals_state_t states[2] = {};
    mx_handle_t wev[2] = {event, quit_event};

    ms = mx_handle_wait_many(2U, wev, signals, MX_TIME_INFINITE, NULL, states);
    if (ms < 0)
        return false;

    return (states[1].satisfied == MX_SIGNAL_SIGNALED);
}

static int thread_fn_1(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    do {
        mx_nanosleep(MX_MSEC(200));
        mx_object_signal(events[1], 0u, MX_SIGNAL_SIGNALED);
    } while (!wait(events[2], events[0]));

    return 0;
}

static int thread_fn_2(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    while (!wait(events[1], events[0])) {
        mx_nanosleep(MX_MSEC(100));
        mx_object_signal(events[2], 0u, MX_SIGNAL_SIGNALED);
    }

    return 0;
}

static bool basic_test(void) {
    BEGIN_TEST;

    mx_handle_t events[3];
    events[0] = mx_event_create(0U);
    ASSERT_GE(events[0], 0, "Error during event create");
    events[1] = mx_event_create(1U);
    ASSERT_GE(events[1], 0, "Error during event create");
    events[2] = mx_event_create(2U);
    ASSERT_GE(events[2], 0, "Error during event create");

    thrd_t threads[4];
    int ret = thrd_create_with_name(&threads[3], thread_fn_1, events, "master");
    ASSERT_EQ(ret, thrd_success, "Error during thread creation");

    for (int ix = 0; ix != 3; ++ix) {
        ret = thrd_create_with_name(&threads[ix], thread_fn_2, events, "worker");
        ASSERT_EQ(ret, thrd_success, "Error during thread creation");
    }

    mx_nanosleep(MX_MSEC(400));
    mx_object_signal(events[0], 0u, MX_SIGNAL_SIGNALED);

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_EQ(thrd_join(threads[ix], NULL), thrd_success, "Error during wait");
    }

    ASSERT_GE(mx_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(mx_handle_close(events[1]), 0, "Error during event-1 close");
    ASSERT_GE(mx_handle_close(events[2]), 0, "Error during event-2 close");
    END_TEST;
}

static int thread_fn_3(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    do {
        mx_nanosleep(MX_MSEC(200));
        mx_object_signal(events[1], MX_SIGNAL_SIGNAL_ALL, MX_SIGNAL_SIGNAL1);
    } while (!wait_user(events[2], events[0], MX_SIGNAL_SIGNAL2));

    return 0;
}

static int thread_fn_4(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    while (!wait_user(events[1], events[0], MX_SIGNAL_SIGNAL1)) {
        mx_nanosleep(MX_MSEC(100));
        mx_object_signal(events[2], MX_SIGNAL_SIGNAL_ALL, MX_SIGNAL_SIGNAL2);
    }

    return 0;
}

static bool user_signals_test(void) {
    BEGIN_TEST;

    mx_handle_t events[3];
    events[0] = mx_event_create(0U);
    ASSERT_GE(events[0], 0, "Error during event create");
    events[1] = mx_event_create(1U);
    ASSERT_GE(events[1], 0, "Error during event create");
    events[2] = mx_event_create(2U);
    ASSERT_GE(events[2], 0, "Error during event create");

    thrd_t threads[4];
    int ret = thrd_create_with_name(&threads[3], thread_fn_3, events, "master");
    ASSERT_EQ(ret, thrd_success, "Error during thread creation");

    for (int ix = 0; ix != 3; ++ix) {
        ret = thrd_create_with_name(&threads[ix], thread_fn_4, events, "workers");
        ASSERT_EQ(ret, thrd_success, "Error during thread creation");
    }

    mx_nanosleep(MX_MSEC(400));
    mx_object_signal(events[0], 0u, MX_SIGNAL_SIGNALED);

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_EQ(thrd_join(threads[ix], NULL), thrd_success, "Error during wait");
    }

    ASSERT_GE(mx_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(mx_handle_close(events[1]), 0, "Error during event-1 close");
    ASSERT_GE(mx_handle_close(events[2]), 0, "Error during event-2 close");
    END_TEST;
}

static int thread_fn_closer(void* arg) {
    mx_nanosleep(MX_MSEC(200));

    mx_handle_t handle = *((mx_handle_t*)arg);
    int rc = (int)mx_handle_close(handle);

    return rc;
}

static bool wait_signals_test(void) {
    BEGIN_TEST;

    mx_handle_t events[3];
    events[0] = mx_event_create(0U);
    ASSERT_GE(events[0], 0, "Error during event create");
    events[1] = mx_event_create(1U);
    ASSERT_GE(events[1], 0, "Error during event create");
    events[2] = mx_event_create(2U);
    ASSERT_GE(events[2], 0, "Error during event create");

    mx_status_t status;
    mx_signals_state_t states[3] = {0};

    const mx_signals_t signals[3] = {
        MX_SIGNAL_SIGNALED, MX_SIGNAL_SIGNALED, MX_SIGNAL_SIGNALED};

    status = mx_handle_wait_one(events[0], signals[0], 1u, &states[0]);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_EQ(states[0].satisfied, 0u, "");

    status = mx_handle_wait_many(3u, events, signals, 1u, NULL, states);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_FALSE(states[0].satisfied || states[1].satisfied || states[2].satisfied, "")

    status = mx_handle_wait_one(events[0], signals[0], 0u, &states[0]);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_EQ(states[0].satisfied, 0u, "");

    status = mx_handle_wait_many(3u, events, signals, 0u, NULL, states);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_FALSE(states[0].satisfied || states[1].satisfied || states[2].satisfied, "")

    ASSERT_GE(mx_object_signal(events[0], 0u, MX_SIGNAL_SIGNALED), 0, "Error during event signal");

    status = mx_handle_wait_one(events[0], signals[0], 1u, &states[0]);
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(states[0].satisfied, MX_SIGNAL_SIGNALED, "Error during wait call");

    uint32_t result_index = 123u;
    status = mx_handle_wait_many(3u, events, signals, 1u, &result_index, states);
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(states[0].satisfied, MX_SIGNAL_SIGNALED, "Error during wait call");
    ASSERT_EQ(result_index, 0u, "Incorrect result index");

    status = mx_handle_wait_one(events[0], signals[0], 0u, &states[0]);
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(states[0].satisfied, MX_SIGNAL_SIGNALED, "Error during wait call");

    ASSERT_GE(mx_object_signal(events[0], MX_SIGNAL_SIGNALED, 0u), 0, "Error during event reset");
    ASSERT_GE(mx_object_signal(events[2], 0u, MX_SIGNAL_SIGNALED), 0, "Error during event signal");
    status = mx_handle_wait_many(3u, events, signals, 1u, &result_index, states);
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(states[2].satisfied, MX_SIGNAL_SIGNALED, "Error during wait call");
    ASSERT_EQ(result_index, 2u, "Incorrect result index");

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, thread_fn_closer, &events[1], "closer");
    ASSERT_EQ(ret, thrd_success, "Error during thread creation");

    status = mx_handle_wait_one(events[1], signals[1], MX_TIME_INFINITE, NULL);
    ASSERT_EQ(status, ERR_HANDLE_CLOSED, "Error during wait");

    ASSERT_EQ(thrd_join(thread, NULL), thrd_success, "Error during thread close");

    ASSERT_GE(mx_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(mx_handle_close(events[2]), 0, "Error during event-2 close");

    END_TEST;
}

static bool reset_test(void) {
    BEGIN_TEST;
    mx_handle_t event = mx_event_create(0U);
    ASSERT_GE(event, 0, "Error during event creation");
    ASSERT_GE(mx_object_signal(event, 0u, MX_SIGNAL_SIGNALED), 0, "Error during event signal");
    ASSERT_GE(mx_object_signal(event, MX_SIGNAL_SIGNALED, 0u), 0, "Error during event reset");

    mx_status_t status;
    status = mx_handle_wait_one(event, MX_SIGNAL_SIGNALED, 1u, NULL);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");

    ASSERT_EQ(mx_handle_close(event), NO_ERROR, "error during handle close");

    END_TEST;
}

static bool wait_many_failures_test(void) {
    BEGIN_TEST;

    ASSERT_EQ(mx_handle_wait_many(0u, NULL, NULL, 1u, NULL, NULL),
              ERR_TIMED_OUT, "wait_many on zero handles should have timed out");

    mx_handle_t handles[2] = {mx_event_create(0u), MX_HANDLE_INVALID};
    ASSERT_GT(handles[0], MX_HANDLE_INVALID, "Error during event creation");

    mx_signals_t signals[2] = {MX_SIGNAL_SIGNALED, MX_SIGNAL_SIGNALED};

    ASSERT_EQ(mx_handle_wait_many(2u, handles, signals, MX_TIME_INFINITE, NULL, NULL),
              ERR_BAD_HANDLE, "Wait-many should have failed with ERR_BAD_HANDLE");

    // Signal the event, to check that wait-many cleaned up correctly.
    ASSERT_EQ(mx_object_signal(handles[0], 0u, MX_SIGNAL_SIGNALED), NO_ERROR,
              "Error during event signal");

    // TODO(vtl): Also test other failure code paths: 1. a handle not supporting waiting (i.e., not
    // having a Waiter), 2. a handle having an I/O port bound.

    ASSERT_EQ(mx_handle_close(handles[0]), NO_ERROR, "Error during handle close");

    END_TEST;
}

BEGIN_TEST_CASE(event_tests)
RUN_TEST(basic_test)
RUN_TEST(user_signals_test)
RUN_TEST(wait_signals_test)
RUN_TEST(reset_test)
RUN_TEST(wait_many_failures_test)
END_TEST_CASE(event_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
