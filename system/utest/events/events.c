// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <unittest/unittest.h>

static bool wait(zx_handle_t event, zx_handle_t quit_event) {
    zx_status_t ms;
    zx_wait_item_t items[2];
    items[0].waitfor = ZX_EVENT_SIGNALED;
    items[0].handle = event;
    items[1].waitfor = ZX_EVENT_SIGNALED;
    items[1].handle = quit_event;

    ms = zx_object_wait_many(items, 2, ZX_TIME_INFINITE);
    if (ms < 0)
        return false;

    return (items[1].pending & ZX_EVENT_SIGNALED);
}

static bool wait_user(zx_handle_t event, zx_handle_t quit_event, zx_signals_t user_signal) {
    zx_status_t ms;

    zx_wait_item_t items[2];
    items[0].waitfor = user_signal;
    items[0].handle = event;
    items[1].waitfor = ZX_EVENT_SIGNALED;
    items[1].handle = quit_event;

    ms = zx_object_wait_many(items, 2, ZX_TIME_INFINITE);
    if (ms < 0)
        return false;

    return (items[1].pending & ZX_EVENT_SIGNALED);
}

static int thread_fn_1(void* arg) {
    zx_handle_t* events = (zx_handle_t*)(arg);

    do {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(200)));
        zx_status_t status = zx_object_signal(events[1], 0u, ZX_EVENT_SIGNALED);
        assert(status == ZX_OK);
    } while (!wait(events[2], events[0]));

    return 0;
}

static int thread_fn_2(void* arg) {
    zx_handle_t* events = (zx_handle_t*)(arg);

    while (!wait(events[1], events[0])) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
        zx_status_t status = zx_object_signal(events[2], 0u, ZX_EVENT_SIGNALED);
        assert(status == ZX_OK);
    }

    return 0;
}

static bool basic_test(void) {
    BEGIN_TEST;

    zx_handle_t events[3];
    ASSERT_EQ(zx_event_create(0u, &events[0]), 0, "Error during event create");
    ASSERT_EQ(zx_event_create(0u, &events[1]), 0, "Error during event create");
    ASSERT_EQ(zx_event_create(0u, &events[2]), 0, "Error during event create");

    thrd_t threads[4];
    int ret = thrd_create_with_name(&threads[3], thread_fn_1, events, "master");
    ASSERT_EQ(ret, thrd_success, "Error during thread creation");

    for (int ix = 0; ix != 3; ++ix) {
        ret = thrd_create_with_name(&threads[ix], thread_fn_2, events, "worker");
        ASSERT_EQ(ret, thrd_success, "Error during thread creation");
    }

    zx_nanosleep(zx_deadline_after(ZX_MSEC(400)));
    zx_object_signal(events[0], 0u, ZX_EVENT_SIGNALED);

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_EQ(thrd_join(threads[ix], NULL), thrd_success, "Error during wait");
    }

    ASSERT_GE(zx_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(zx_handle_close(events[1]), 0, "Error during event-1 close");
    ASSERT_GE(zx_handle_close(events[2]), 0, "Error during event-2 close");
    END_TEST;
}

static int thread_fn_3(void* arg) {
    zx_handle_t* events = (zx_handle_t*)(arg);

    do {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(200)));
        zx_object_signal(events[1], ZX_USER_SIGNAL_ALL, ZX_USER_SIGNAL_1);
    } while (!wait_user(events[2], events[0], ZX_USER_SIGNAL_2));

    return 0;
}

static int thread_fn_4(void* arg) {
    zx_handle_t* events = (zx_handle_t*)(arg);

    while (!wait_user(events[1], events[0], ZX_USER_SIGNAL_1)) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
        zx_object_signal(events[2], ZX_USER_SIGNAL_ALL, ZX_USER_SIGNAL_2);
    }

    return 0;
}

static bool user_signals_test(void) {
    BEGIN_TEST;

    zx_handle_t events[3];
    ASSERT_GE(zx_event_create(0U, &events[0]), 0, "Error during event create");
    ASSERT_GE(zx_event_create(0U, &events[1]), 0, "Error during event create");
    ASSERT_GE(zx_event_create(0U, &events[2]), 0, "Error during event create");

    thrd_t threads[4];
    int ret = thrd_create_with_name(&threads[3], thread_fn_3, events, "master");
    ASSERT_EQ(ret, thrd_success, "Error during thread creation");

    for (int ix = 0; ix != 3; ++ix) {
        ret = thrd_create_with_name(&threads[ix], thread_fn_4, events, "workers");
        ASSERT_EQ(ret, thrd_success, "Error during thread creation");
    }

    zx_nanosleep(zx_deadline_after(ZX_MSEC(400)));
    zx_object_signal(events[0], 0u, ZX_EVENT_SIGNALED);

    for (int ix = 0; ix != 4; ++ix) {
        ASSERT_EQ(thrd_join(threads[ix], NULL), thrd_success, "Error during wait");
    }

    ASSERT_GE(zx_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(zx_handle_close(events[1]), 0, "Error during event-1 close");
    ASSERT_GE(zx_handle_close(events[2]), 0, "Error during event-2 close");
    END_TEST;
}

static int thread_fn_closer(void* arg) {
    zx_nanosleep(zx_deadline_after(ZX_MSEC(200)));

    zx_handle_t handle = *((zx_handle_t*)arg);
    int rc = (int)zx_handle_close(handle);

    return rc;
}

static bool wait_signals_test(void) {
    BEGIN_TEST;

    zx_handle_t events[3];
    ASSERT_EQ(zx_event_create(0U, &events[0]), 0, "Error during event create");
    ASSERT_EQ(zx_event_create(0U, &events[1]), 0, "Error during event create");
    ASSERT_EQ(zx_event_create(0U, &events[2]), 0, "Error during event create");

    zx_status_t status;
    zx_signals_t pending;

    zx_wait_item_t items[3];
    items[0].waitfor = ZX_EVENT_SIGNALED;
    items[0].handle = events[0];
    items[1].waitfor = ZX_EVENT_SIGNALED;
    items[1].handle = events[1];
    items[2].waitfor = ZX_EVENT_SIGNALED;
    items[2].handle = events[2];

    status = zx_object_wait_one(events[0], ZX_EVENT_SIGNALED, zx_deadline_after(1u), &pending);
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_EQ(pending, 0u, "");

    status = zx_object_wait_many(items, 3, zx_deadline_after(1));
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_EQ(items[0].pending, 0u, "");
    ASSERT_EQ(items[1].pending, 0u, "");
    ASSERT_EQ(items[2].pending, 0u, "");

    status = zx_object_wait_one(events[0], ZX_EVENT_SIGNALED, 0u, &pending);
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_EQ(pending, 0u, "");

    status = zx_object_wait_many(items, 3, 0);
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT, "wait should have timeout");
    ASSERT_EQ(items[0].pending, 0u, "");
    ASSERT_EQ(items[1].pending, 0u, "");
    ASSERT_EQ(items[2].pending, 0u, "");

    ASSERT_GE(zx_object_signal(events[0], 0u, ZX_EVENT_SIGNALED), 0, "Error during event signal");

    status = zx_object_wait_one(events[0], ZX_EVENT_SIGNALED, zx_deadline_after(1u), &pending);
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(pending, ZX_EVENT_SIGNALED, "Error during wait call");

    status = zx_object_wait_many(items, 3, zx_deadline_after(1));
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(items[0].pending,
        ZX_EVENT_SIGNALED, "Error during wait call");

    status = zx_object_wait_one(events[0], ZX_EVENT_SIGNALED, 0u, &pending);
    ASSERT_EQ(status, ZX_OK, "wait failed");
    ASSERT_EQ(pending, ZX_EVENT_SIGNALED, "Error during wait call");

    ASSERT_GE(zx_object_signal(events[0], ZX_EVENT_SIGNALED, 0u), 0, "Error during event reset");
    ASSERT_GE(zx_object_signal(events[2], 0u, ZX_EVENT_SIGNALED), 0, "Error during event signal");
    status = zx_object_wait_many(items, 3, zx_deadline_after(1));
    ASSERT_EQ(status, 0, "wait failed");
    ASSERT_EQ(items[2].pending,
        ZX_EVENT_SIGNALED, "Error during wait call");

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, thread_fn_closer, &events[1], "closer");
    ASSERT_EQ(ret, thrd_success, "Error during thread creation");

    status = zx_object_wait_one(events[1], ZX_EVENT_SIGNALED, ZX_TIME_INFINITE, NULL);
    ASSERT_EQ(status, ZX_ERR_CANCELED, "Error during wait");

    ASSERT_EQ(thrd_join(thread, NULL), thrd_success, "Error during thread close");

    ASSERT_GE(zx_handle_close(events[0]), 0, "Error during event-0 close");
    ASSERT_GE(zx_handle_close(events[2]), 0, "Error during event-2 close");

    END_TEST;
}

static bool reset_test(void) {
    BEGIN_TEST;
    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0U, &event), 0, "Error during event creation");
    ASSERT_GE(zx_object_signal(event, 0u, ZX_EVENT_SIGNALED), 0, "Error during event signal");
    ASSERT_GE(zx_object_signal(event, ZX_EVENT_SIGNALED, 0u), 0, "Error during event reset");

    zx_status_t status;
    status = zx_object_wait_one(event, ZX_EVENT_SIGNALED, zx_deadline_after(1u), NULL);
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT, "wait should have timeout");

    ASSERT_EQ(zx_handle_close(event), ZX_OK, "error during handle close");

    END_TEST;
}

static bool wait_many_failures_test(void) {
    BEGIN_TEST;

    ASSERT_EQ(zx_object_wait_many(NULL, 0, zx_deadline_after(1)),
              ZX_ERR_TIMED_OUT, "wait_many on zero handles should have timed out");

    zx_handle_t handles[2] = { ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};
    ASSERT_EQ(zx_event_create(0u, &handles[0]), 0, "Error during event creation");

    zx_wait_item_t items[2];
    items[0].handle = handles[0];
    items[0].waitfor = ZX_EVENT_SIGNALED;
    items[1].handle = handles[1];
    items[1].waitfor = ZX_EVENT_SIGNALED;
    ASSERT_EQ(zx_object_wait_many(items, 2, ZX_TIME_INFINITE),
              ZX_ERR_BAD_HANDLE, "Wait-many should have failed with ZX_ERR_BAD_HANDLE");

    // Signal the event, to check that wait-many cleaned up correctly.
    ASSERT_EQ(zx_object_signal(handles[0], 0u, ZX_EVENT_SIGNALED), ZX_OK,
              "Error during event signal");

    // TODO(vtl): Also test other failure code paths: 1. a handle not supporting waiting (i.e., not
    // having a Waiter), 2. a handle having an I/O port bound.

    ASSERT_EQ(zx_handle_close(handles[0]), ZX_OK, "Error during handle close");

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
