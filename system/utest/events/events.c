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

#define CHECK_MX_STATUS(call)                                                    \
    do {                                                                         \
        mx_status_t status = (call);                                             \
        if (status < 0) {                                                        \
            printf("%s:%d: %s failed: %d\n", __FILE__, __LINE__, #call, status); \
            return __LINE__;                                                     \
        }                                                                        \
    } while (0)

#define FAIL_TEST                                    \
    do {                                             \
        printf("%s:%d: failed", __FILE__, __LINE__); \
        return __LINE__;                             \
    } while (0)

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

static int basic_test(void) {
    printf("basic event test\n");

    mx_handle_t events[3];
    CHECK_MX_STATUS(events[0] = _magenta_event_create(0U));
    CHECK_MX_STATUS(events[1] = _magenta_event_create(1U));
    CHECK_MX_STATUS(events[2] = _magenta_event_create(2U));

    mx_handle_t threads[4];
    CHECK_MX_STATUS(threads[3] = _magenta_thread_create(thread_fn_1, events, "master", 7));

    for (int ix = 0; ix != 3; ++ix) {
        CHECK_MX_STATUS(threads[ix] = _magenta_thread_create(thread_fn_2, events, "workers", 8));
    }

    _magenta_nanosleep(400 * 1000 * 1000);
    _magenta_event_signal(events[0]);

    for (int ix = 0; ix != 4; ++ix) {
        CHECK_MX_STATUS(_magenta_handle_wait_one(threads[ix], MX_SIGNAL_SIGNALED,
                                                 MX_TIME_INFINITE, NULL, NULL));
        CHECK_MX_STATUS(_magenta_handle_close(threads[ix]));
    }

    CHECK_MX_STATUS(_magenta_handle_close(events[0]));
    CHECK_MX_STATUS(_magenta_handle_close(events[1]));
    CHECK_MX_STATUS(_magenta_handle_close(events[2]));
    return 0;
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

static int user_signals_test(void) {
    printf("user signals event test\n");

    mx_handle_t events[3];
    CHECK_MX_STATUS(events[0] = _magenta_event_create(0U));
    CHECK_MX_STATUS(events[1] = _magenta_event_create(1U));
    CHECK_MX_STATUS(events[2] = _magenta_event_create(2U));

    mx_handle_t threads[4];
    CHECK_MX_STATUS(threads[3] = _magenta_thread_create(thread_fn_3, events, "master", 7));

    for (int ix = 0; ix != 3; ++ix) {
        CHECK_MX_STATUS(threads[ix] = _magenta_thread_create(thread_fn_4, events, "workers", 8));
    }

    _magenta_nanosleep(400 * 1000 * 1000);
    _magenta_event_signal(events[0]);

    for (int ix = 0; ix != 4; ++ix) {
        CHECK_MX_STATUS(_magenta_handle_wait_one(threads[ix], MX_SIGNAL_SIGNALED,
                                                 MX_TIME_INFINITE, NULL, NULL));
        CHECK_MX_STATUS(_magenta_handle_close(threads[ix]));
    }

    CHECK_MX_STATUS(_magenta_handle_close(events[0]));
    CHECK_MX_STATUS(_magenta_handle_close(events[1]));
    CHECK_MX_STATUS(_magenta_handle_close(events[2]));
    return 0;
}

static int thread_fn_closer(void* arg) {
    _magenta_nanosleep(1000000);

    mx_handle_t handle = *((mx_handle_t*)arg);
    int rc = (int)_magenta_handle_close(handle);

    _magenta_thread_exit();
    return rc;
}

static int wait_signals_test(void) {
    printf("wait signals event test\n");

    mx_handle_t events[3];
    CHECK_MX_STATUS(events[0] = _magenta_event_create(0U));
    CHECK_MX_STATUS(events[1] = _magenta_event_create(1U));
    CHECK_MX_STATUS(events[2] = _magenta_event_create(2U));

    mx_status_t status;
    mx_signals_t satisfied[3] = {0};

    const mx_signals_t signals[3] = {
        MX_SIGNAL_SIGNALED, MX_SIGNAL_SIGNALED, MX_SIGNAL_SIGNALED};

    status = _magenta_handle_wait_one(events[0], signals[0], 1u, &satisfied[0], NULL);
    if (status != ERR_TIMED_OUT)
        FAIL_TEST;
    if (satisfied[0])
        FAIL_TEST;

    status = _magenta_handle_wait_many(3u, events, signals, 1u, satisfied, NULL);
    if (status != ERR_TIMED_OUT)
        FAIL_TEST;
    if (satisfied[0] || satisfied[1] || satisfied[2])
        FAIL_TEST;

    status = _magenta_handle_wait_one(events[0], signals[0], 0u, &satisfied[0], NULL);
    if (status != ERR_TIMED_OUT)
        FAIL_TEST;
    if (satisfied[0])
        FAIL_TEST;

    status = _magenta_handle_wait_many(3u, events, signals, 0u, satisfied, NULL);
    if (status != ERR_TIMED_OUT)
        return 1;
    if (satisfied[1] || satisfied[1])
        FAIL_TEST;

    CHECK_MX_STATUS(_magenta_event_signal(events[0]));

    status = _magenta_handle_wait_one(events[0], signals[0], 1u, &satisfied[0], NULL);
    if (status)
        FAIL_TEST;
    if (satisfied[0] != MX_SIGNAL_SIGNALED)
        FAIL_TEST;

    status = _magenta_handle_wait_many(3u, events, signals, 1u, satisfied, NULL);
    if (status)
        FAIL_TEST;
    if (satisfied[0] != MX_SIGNAL_SIGNALED)
        FAIL_TEST;

    status = _magenta_handle_wait_one(events[0], signals[0], 0u, &satisfied[0], NULL);
    if (status)
        FAIL_TEST;
    if (satisfied[0] != MX_SIGNAL_SIGNALED)
        FAIL_TEST;

    mx_handle_t thread;
    CHECK_MX_STATUS(thread = _magenta_thread_create(thread_fn_closer, &events[1], "closer", 7));

    status = _magenta_handle_wait_one(events[1], signals[1], MX_TIME_INFINITE, NULL, NULL);
    if (status != ERR_CANCELLED)
        FAIL_TEST;

    CHECK_MX_STATUS(_magenta_handle_wait_one(
        thread, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL));

    CHECK_MX_STATUS(_magenta_handle_close(thread));

    CHECK_MX_STATUS(_magenta_handle_close(events[0]));
    CHECK_MX_STATUS(_magenta_handle_close(events[2]));

    return 0;
}

static int reset_test(void) {
    printf("reset event test\n");

    mx_handle_t event;
    CHECK_MX_STATUS(event = _magenta_event_create(0U));
    CHECK_MX_STATUS(_magenta_event_signal(event));
    CHECK_MX_STATUS(_magenta_event_reset(event));

    mx_status_t status;
    status = _magenta_handle_wait_one(event, MX_SIGNAL_SIGNALED, 1u, NULL, NULL);
    if (status != ERR_TIMED_OUT)
        FAIL_TEST;

    CHECK_MX_STATUS(_magenta_handle_close(event));

    return 0;
}

#define EXIT_TEST(n, r)                                \
    if ((r)) {                                         \
        printf("event test %d: error %d\n", (n), (r)); \
        return r;                                      \
    }

int main(void) {
    int res;
    res = basic_test();
    EXIT_TEST(1, res);
    res = user_signals_test();
    EXIT_TEST(2, res);
    res = wait_signals_test();
    EXIT_TEST(3, res);
    res = reset_test();
    EXIT_TEST(4, res);

    printf("event test done\n");
    return 0;
}
