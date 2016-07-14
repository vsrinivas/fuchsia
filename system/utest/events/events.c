// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <runtime/thread.h>
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
        mx_nanosleep(200 * 1000 * 1000);
        mx_event_signal(events[1]);
    } while (!wait(events[2], events[0]));

    return 0;
}

static int thread_fn_2(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    while (!wait(events[1], events[0])) {
        mx_nanosleep(100 * 1000 * 1000);
        mx_event_signal(events[2]);
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

    mxr_thread_t *threads[4];
    mx_status_t status = mxr_thread_create(thread_fn_1, events, "master", &threads[3]);
    ASSERT_EQ(status, 0, "Error during thread creation");

    for (int ix = 0; ix != 3; ++ix) {
        status = mxr_thread_create(thread_fn_2, events, "workers", &threads[ix]);
        ASSERT_EQ(status, 0, "Error during thread creation");
    }

    mx_nanosleep(400 * 1000 * 1000);
    mx_event_signal(events[0]);

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_EQ(mxr_thread_join(threads[ix], NULL), 0, "Error during wait");
    }

    ASSERT_GE(mx_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(mx_handle_close(events[1]), 0, "Error during event-1 close");
    ASSERT_GE(mx_handle_close(events[2]), 0, "Error during event-2 close");
    END_TEST;
}

static int thread_fn_3(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    do {
        mx_nanosleep(200 * 1000 * 1000);
        mx_object_signal(events[1], MX_SIGNAL_USER1, MX_SIGNAL_USER_ALL);
    } while (!wait_user(events[2], events[0], MX_SIGNAL_USER2));

    return 0;
}

static int thread_fn_4(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    while (!wait_user(events[1], events[0], MX_SIGNAL_USER1)) {
        mx_nanosleep(100 * 1000 * 1000);
        mx_object_signal(events[2], MX_SIGNAL_USER2, MX_SIGNAL_USER_ALL);
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

    mxr_thread_t *threads[4];
    mx_status_t status = mxr_thread_create(thread_fn_3, events, "master", &threads[3]);
    ASSERT_EQ(status, 0, "Error during thread creation");

    for (int ix = 0; ix != 3; ++ix) {
        status = mxr_thread_create(thread_fn_4, events, "workers", &threads[ix]);
        ASSERT_EQ(status, 0, "Error during thread creation");
    }

    mx_nanosleep(400 * 1000 * 1000);
    mx_event_signal(events[0]);

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_EQ(mxr_thread_join(threads[ix], NULL), 0, "Error during wait");
    }

    ASSERT_GE(mx_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(mx_handle_close(events[1]), 0, "Error during event-1 close");
    ASSERT_GE(mx_handle_close(events[2]), 0, "Error during event-2 close");
    END_TEST;
}

static int thread_fn_closer(void* arg) {
    mx_nanosleep(200 * 1000 * 1000);

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

    ASSERT_GE(mx_event_signal(events[0]), 0, "Error during event signal");

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

    ASSERT_GE(mx_event_reset(events[0]), 0, "Error during event reset");
    ASSERT_GE(mx_event_signal(events[2]), 0, "Error during event signal");
    status = mx_handle_wait_many(3u, events, signals, 1u, &result_index, states);
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(states[2].satisfied, MX_SIGNAL_SIGNALED, "Error during wait call");
    ASSERT_EQ(result_index, 2u, "Incorrect result index");

    mxr_thread_t *thread;
    status = mxr_thread_create(thread_fn_closer, &events[1], "closer", &thread);
    ASSERT_EQ(status, 0, "Error during thread creation");

    status = mx_handle_wait_one(events[1], signals[1], MX_TIME_INFINITE, NULL);
    ASSERT_EQ(status, ERR_CANCELLED, "Error during wait");

    ASSERT_EQ(mxr_thread_join(thread, NULL), 0, "Error during thread close");

    ASSERT_GE(mx_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(mx_handle_close(events[2]), 0, "Error during event-2 close");

    END_TEST;
}

static bool reset_test(void) {
    BEGIN_TEST;
    mx_handle_t event = mx_event_create(0U);
    ASSERT_GE(event, 0, "Error during event creation");
    ASSERT_GE(mx_event_signal(event), 0, "Error during event signal");
    ASSERT_GE(mx_event_reset(event), 0, "Error during event reset");

    mx_status_t status;
    status = mx_handle_wait_one(event, MX_SIGNAL_SIGNALED, 1u, NULL);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");

    ASSERT_GE(mx_handle_close(event), 0, "error during handle close");

    END_TEST;
}

BEGIN_TEST_CASE(event_tests)
RUN_TEST(basic_test)
RUN_TEST(user_signals_test)
RUN_TEST(wait_signals_test)
RUN_TEST(reset_test)
END_TEST_CASE(event_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
