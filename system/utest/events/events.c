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
#include <mxu/unittest.h>

static bool wait(mx_handle_t event, mx_handle_t quit_event) {
    mx_status_t ms;
    mx_signals_t signals[2] = {MX_SIGNAL_SIGNALED, MX_SIGNAL_SIGNALED};
    mx_signals_t satisfied[2] = {};
    mx_signals_t satisfiable[2] = {};
    mx_handle_t wev[2] = {event, quit_event};

    ms = _magenta_handle_wait_many(2U, wev, signals, MX_TIME_INFINITE, satisfied, satisfiable);
    if (ms < 0)
        return false;

    return (satisfied[1] == MX_SIGNAL_SIGNALED);
}

static bool wait_user(mx_handle_t event, mx_handle_t quit_event, mx_signals_t user_signal) {
    mx_status_t ms;
    mx_signals_t signals[2] = {user_signal, MX_SIGNAL_SIGNALED};
    mx_signals_t satisfied[2] = {};
    mx_signals_t satisfiable[2] = {};
    mx_handle_t wev[2] = {event, quit_event};

    ms = _magenta_handle_wait_many(2U, wev, signals, MX_TIME_INFINITE, satisfied, satisfiable);
    if (ms < 0)
        return false;

    return (satisfied[1] == MX_SIGNAL_SIGNALED);
}

static int thread_fn_1(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    do {
        _magenta_nanosleep(200 * 1000 * 1000);
        _magenta_event_signal(events[1]);
    } while (!wait(events[2], events[0]));

    _magenta_thread_exit();
    return 0;
}

static int thread_fn_2(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    while (!wait(events[1], events[0])) {
        _magenta_nanosleep(100 * 1000 * 1000);
        _magenta_event_signal(events[2]);
    }

    _magenta_thread_exit();
    return 0;
}

static bool basic_test(void) {
    BEGIN_TEST;

    mx_handle_t events[3];
    events[0] = _magenta_event_create(0U);
    ASSERT_GE(events[0], 0, "Error during event create");
    events[1] = _magenta_event_create(1U);
    ASSERT_GE(events[1], 0, "Error during event create");
    events[2] = _magenta_event_create(2U);
    ASSERT_GE(events[2], 0, "Error during event create");

    mx_handle_t threads[4];
    threads[3] = _magenta_thread_create(thread_fn_1, events, "master", 7);
    ASSERT_GE(threads[3], 0, "Error during thread creation");

    for (int ix = 0; ix != 3; ++ix) {
        threads[ix] = _magenta_thread_create(thread_fn_2, events, "workers", 8);
        ASSERT_GE(threads[ix], 0, "Error during thread creation");
    }

    _magenta_nanosleep(400 * 1000 * 1000);
    _magenta_event_signal(events[0]);

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_GE(_magenta_handle_wait_one(threads[ix], MX_SIGNAL_SIGNALED,
                                           MX_TIME_INFINITE, NULL, NULL),
                  0, "Error during wait");
        ASSERT_GE(_magenta_handle_close(threads[ix]), 0, "Error during thread close");
    }

    ASSERT_GE(_magenta_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(_magenta_handle_close(events[1]), 0, "Error during event-1 close");
    ASSERT_GE(_magenta_handle_close(events[2]), 0, "Error during event-2 close");
    END_TEST;
}

static int thread_fn_3(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    do {
        _magenta_nanosleep(200 * 1000 * 1000);
        _magenta_object_signal(events[1], MX_SIGNAL_USER1, MX_SIGNAL_USER_ALL);
    } while (!wait_user(events[2], events[0], MX_SIGNAL_USER2));

    _magenta_thread_exit();
    return 0;
}

static int thread_fn_4(void* arg) {
    mx_handle_t* events = (mx_handle_t*)(arg);

    while (!wait_user(events[1], events[0], MX_SIGNAL_USER1)) {
        _magenta_nanosleep(100 * 1000 * 1000);
        _magenta_object_signal(events[2], MX_SIGNAL_USER2, MX_SIGNAL_USER_ALL);
    }

    _magenta_thread_exit();
    return 0;
}

static bool user_signals_test(void) {
    BEGIN_TEST;

    mx_handle_t events[3];
    events[0] = _magenta_event_create(0U);
    ASSERT_GE(events[0], 0, "Error during event create");
    events[1] = _magenta_event_create(1U);
    ASSERT_GE(events[1], 0, "Error during event create");
    events[2] = _magenta_event_create(2U);
    ASSERT_GE(events[2], 0, "Error during event create");

    mx_handle_t threads[4];
    threads[3] = _magenta_thread_create(thread_fn_3, events, "master", 7);
    ASSERT_GE(threads[3], 0, "Error during thread creation");

    for (int ix = 0; ix != 3; ++ix) {
        threads[ix] = _magenta_thread_create(thread_fn_4, events, "workers", 8);
        ASSERT_GE(threads[ix], 0, "Error during thread creation");
    }

    _magenta_nanosleep(400 * 1000 * 1000);
    _magenta_event_signal(events[0]);

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_GE(_magenta_handle_wait_one(threads[ix], MX_SIGNAL_SIGNALED,
                                           MX_TIME_INFINITE, NULL, NULL),
                  0, "Error during wait");
        ASSERT_GE(_magenta_handle_close(threads[ix]), 0, "Error during thread close");
    }

    ASSERT_GE(_magenta_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(_magenta_handle_close(events[1]), 0, "Error during event-1 close");
    ASSERT_GE(_magenta_handle_close(events[2]), 0, "Error during event-2 close");
    END_TEST;
}

static int thread_fn_closer(void* arg) {
    _magenta_nanosleep(200 * 1000 * 1000);

    mx_handle_t handle = *((mx_handle_t*)arg);
    int rc = (int)_magenta_handle_close(handle);

    _magenta_thread_exit();
    return rc;
}

static bool wait_signals_test(void) {
    BEGIN_TEST;

    mx_handle_t events[3];
    events[0] = _magenta_event_create(0U);
    ASSERT_GE(events[0], 0, "Error during event create");
    events[1] = _magenta_event_create(1U);
    ASSERT_GE(events[1], 0, "Error during event create");
    events[2] = _magenta_event_create(2U);
    ASSERT_GE(events[2], 0, "Error during event create");

    mx_status_t status;
    mx_signals_t satisfied[3] = {0};

    const mx_signals_t signals[3] = {
        MX_SIGNAL_SIGNALED, MX_SIGNAL_SIGNALED, MX_SIGNAL_SIGNALED};

    status = _magenta_handle_wait_one(events[0], signals[0], 1u, &satisfied[0], NULL);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_EQ(satisfied[0], 0u, "");

    status = _magenta_handle_wait_many(3u, events, signals, 1u, satisfied, NULL);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_FALSE(satisfied[0] || satisfied[1] || satisfied[2], "")

    status = _magenta_handle_wait_one(events[0], signals[0], 0u, &satisfied[0], NULL);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_EQ(satisfied[0], 0u, "");

    status = _magenta_handle_wait_many(3u, events, signals, 0u, satisfied, NULL);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_FALSE(satisfied[0] || satisfied[1] || satisfied[2], "")

    ASSERT_GE(_magenta_event_signal(events[0]), 0, "Error suring event signal");

    status = _magenta_handle_wait_one(events[0], signals[0], 1u, &satisfied[0], NULL);
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(satisfied[0], MX_SIGNAL_SIGNALED, "Error during wait call");

    status = _magenta_handle_wait_many(3u, events, signals, 1u, satisfied, NULL);
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(satisfied[0], MX_SIGNAL_SIGNALED, "Error during wait call");

    status = _magenta_handle_wait_one(events[0], signals[0], 0u, &satisfied[0], NULL);
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(satisfied[0], MX_SIGNAL_SIGNALED, "Error during wait call");

    mx_handle_t thread;
    thread = _magenta_thread_create(thread_fn_closer, &events[1], "closer", 7);
    ASSERT_GE(thread, 0, "Error during thread creation");

    status = _magenta_handle_wait_one(events[1], signals[1], MX_TIME_INFINITE, NULL, NULL);
    ASSERT_EQ(status, ERR_CANCELLED, "Error during wait");

    ASSERT_GE(_magenta_handle_wait_one(
                  thread, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL),
              0, "Error during wait");

    ASSERT_GE(_magenta_handle_close(thread), 0, "Error during thread close");

    ASSERT_GE(_magenta_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(_magenta_handle_close(events[2]), 0, "Error during event-2 close");

    END_TEST;
}

static bool reset_test(void) {
    BEGIN_TEST;
    mx_handle_t event = _magenta_event_create(0U);
    ASSERT_GE(event, 0, "Error during event creation");
    ASSERT_GE(_magenta_event_signal(event), 0, "Error during event signal");
    ASSERT_GE(_magenta_event_reset(event), 0, "Error during event reset");

    mx_status_t status;
    status = _magenta_handle_wait_one(event, MX_SIGNAL_SIGNALED, 1u, NULL, NULL);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait should have timeout");

    ASSERT_GE(_magenta_handle_close(event), 0, "error during handle close");

    END_TEST;
}

BEGIN_TEST_CASE(event_tests)
RUN_TEST(basic_test)
RUN_TEST(user_signals_test)
RUN_TEST(wait_signals_test)
RUN_TEST(reset_test)
END_TEST_CASE(event_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
