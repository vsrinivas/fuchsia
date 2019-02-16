// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <unittest/unittest.h>

#include <zircon/compiler.h>

#define ASSERT_NOT_REACHED() \
    assert(0)

// We have to poll a thread's state as there is no way to wait for it to
// transition states. Wait this amount of time. Generally the thread won't
// take very long so this is a compromise between polling too frequently and
// waiting too long.
#define THREAD_BLOCKED_WAIT_DURATION ZX_MSEC(1)

enum message {
    MSG_EXIT,
    MSG_EXITED,
    MSG_WAIT_EVENT,
    MSG_WAIT_EVENT_SIGNALED,
    MSG_WAIT_EVENT_CANCELLED,
    MSG_PING,
    MSG_PONG,
    MSG_READ_CANCELLED,
};

enum wait_result {
    WAIT_READABLE,
    WAIT_SIGNALED,
    WAIT_CLOSED,
    WAIT_CANCELLED,
};

typedef struct thread_data {
    int thread_num;
    zx_handle_t channel;
} thread_data_t;

typedef struct wait_data {
    zx_handle_t handle;
    zx_handle_t signals;
    zx_duration_t timeout;
    zx_status_t status;
} wait_data_t;

// [0] is used by main thread
// [1] is used by worker thread
static zx_handle_t thread1_channel[2];
static zx_handle_t thread2_channel[2];

static atomic_int in_wait_event = ATOMIC_VAR_INIT(0);
static zx_handle_t event_handle;

// Wait until |handle| is readable or peer is closed (or wait is cancelled).

static bool wait_readable(zx_handle_t handle, enum wait_result* result) {
    zx_signals_t pending;
    zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    zx_time_t deadline = ZX_TIME_INFINITE;
    zx_status_t status = zx_object_wait_one(handle, signals, deadline, &pending);
    if (status == ZX_ERR_CANCELED) {
        *result = WAIT_CANCELLED;
        return true;
    }
    ASSERT_GE(status, 0, "handle wait one failed");
    if ((pending & ZX_CHANNEL_READABLE) != 0) {
        *result = WAIT_READABLE;
        return true;
    }
    unittest_printf("wait_readable: peer closed\n");
    *result = WAIT_CLOSED;
    return true;
}

// N.B. This must use zx_object_wait_one.
// See wait_thread_blocked_in_wait_event.
static bool wait_event_worker(zx_handle_t handle, enum wait_result* result) {
    zx_signals_t pending;
    zx_signals_t signals = ZX_EVENT_SIGNALED;
    zx_time_t deadline = ZX_TIME_INFINITE;
    zx_status_t status = zx_object_wait_one(handle, signals, deadline, &pending);
    if (status == ZX_ERR_CANCELED) {
        *result = WAIT_CANCELLED;
        return true;
    }
    ASSERT_GE(status, 0, "handle wait one failed");
    ASSERT_NE(pending & ZX_EVENT_SIGNALED, 0u,
              "unexpected return in wait_signaled");
    *result = WAIT_SIGNALED;
    return true;
}

static bool wait_event(enum wait_result* result) {
    atomic_store(&in_wait_event, 1);
    bool pass = wait_event_worker(event_handle, result);
    atomic_store(&in_wait_event, 0);
    return pass;
}

// Wait for |thread| to be blocked inside wait_event().
// We wait forever and let Unittest's watchdog handle errors.
// Returns true if |thread| successfully enters the blocked state,
// false if there's an error somewhere.
// N.B. We assume wait_event() uses zx_object_wait_one.
static bool wait_thread_blocked_in_wait_event(zx_handle_t thread) {
    while (true) {
        if (atomic_load(&in_wait_event)) {
            zx_info_thread_t info;
            ASSERT_EQ(zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info), NULL, NULL),
                      ZX_OK, "");
            if (info.state == ZX_THREAD_STATE_BLOCKED_WAIT_ONE)
                break;
        }
        zx_nanosleep(zx_deadline_after(THREAD_BLOCKED_WAIT_DURATION));
    }

    return true;
}

static zx_status_t channel_create(zx_handle_t* handle0, zx_handle_t* handle1) {
    return zx_channel_create(0, handle0, handle1);
}

static bool send_msg(zx_handle_t handle, enum message msg) {
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    zx_status_t status =
        zx_channel_write(handle, 0, &data, sizeof(data), NULL, 0);
    ASSERT_GE(status, 0, "message write failed");
    return true;
}

static bool recv_msg(zx_handle_t handle, enum message* msg) {
    uint64_t data;

    unittest_printf("waiting for message on handle %u\n", handle);
    enum wait_result result;
    ASSERT_TRUE(wait_readable(handle, &result), "Error during waiting for read call");
    ASSERT_NE(result, (enum wait_result)WAIT_CLOSED, "peer closed while trying to read message");
    switch (result) {
    case WAIT_READABLE:
        break;
    case WAIT_CANCELLED:
        unittest_printf("read wait cancelled\n");
        *msg = MSG_READ_CANCELLED;
        return true;
    default:
        ASSERT_TRUE(false, "Invalid read-wait status");
    }

    uint32_t num_bytes = sizeof(data);

    ASSERT_GE(zx_channel_read(handle, 0, &data, NULL, num_bytes, 0, &num_bytes, NULL), 0,
              "Error while reading message");
    EXPECT_EQ(num_bytes, sizeof(data), "unexpected message size");
    if (num_bytes != sizeof(data)) {
        zx_thread_exit();
    }
    *msg = (enum message)data;
    unittest_printf("received message %d\n", *msg);
    return true;
}

static bool msg_loop(zx_handle_t channel) {
    bool my_done_tests = false;
    while (!my_done_tests) {
        enum message msg;
        enum wait_result result;
        ASSERT_TRUE(recv_msg(channel, &msg), "Error while receiving msg");
        switch (msg) {
        case MSG_EXIT:
            my_done_tests = true;
            break;
        case MSG_PING:
            send_msg(channel, MSG_PONG);
            break;
        case MSG_WAIT_EVENT:
            ASSERT_TRUE(wait_event(&result), "Error during wait signal call");
            switch (result) {
            case WAIT_SIGNALED:
                send_msg(channel, MSG_WAIT_EVENT_SIGNALED);
                break;
            case WAIT_CANCELLED:
                send_msg(channel, MSG_WAIT_EVENT_CANCELLED);
                break;
            default:
                ASSERT_TRUE(false, "Invalid wait signal");
            }
            break;
        default:
            unittest_printf("unknown message received: %d", msg);
            break;
        }
    }
    return true;
}

static int worker_thread_func(void* arg) {
    thread_data_t* data = arg;
    msg_loop(data->channel);
    unittest_printf("thread %d exiting\n", data->thread_num);
    send_msg(data->channel, MSG_EXITED);
    return 0;
}


static int wait_thread_func(void* arg) {
    wait_data_t* data = arg;
    zx_signals_t observed;
    data->status = zx_object_wait_one(data->handle, data->signals, zx_deadline_after(data->timeout),
                                      &observed);
    return 0;
}

bool handle_wait_test(void) {
    BEGIN_TEST;

    ASSERT_GE(channel_create(&thread1_channel[0], &thread1_channel[1]), 0, "channel creation failed");
    ASSERT_GE(channel_create(&thread2_channel[0], &thread2_channel[1]), 0, "channel creation failed");

    thread_data_t thread1_data = {1, thread1_channel[1]};
    thread_data_t thread2_data = {2, thread2_channel[1]};

    thrd_t thread1;
    ASSERT_EQ(thrd_create(&thread1, worker_thread_func, &thread1_data), thrd_success,
              "thread creation failed");
    thrd_t thread2;
    ASSERT_EQ(thrd_create(&thread2, worker_thread_func, &thread2_data), thrd_success,
              "thread creation failed");
    unittest_printf("threads started\n");

    event_handle = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_event_create(0u, &event_handle), 0, "");
    ASSERT_NE(event_handle, ZX_HANDLE_INVALID, "event creation failed");

    enum message msg;
    send_msg(thread1_channel[0], MSG_PING);
    ASSERT_TRUE(recv_msg(thread1_channel[0], &msg), "Error while receiving msg");
    EXPECT_EQ(msg, (enum message)MSG_PONG, "unexpected reply to ping1");

    send_msg(thread1_channel[0], MSG_WAIT_EVENT);

    send_msg(thread2_channel[0], MSG_PING);
    ASSERT_TRUE(recv_msg(thread2_channel[0], &msg), "Error while receiving msg");
    EXPECT_EQ(msg, (enum message)MSG_PONG, "unexpected reply to ping2");

    // Verify thread 1 is woken up when we close the handle it's waiting on
    // when there exists a duplicate of the handle.
    // But first make sure the thread is waiting on |event_handle| before we
    // close it.
    zx_handle_t thread1_handle = thrd_get_zx_handle(thread1);
    ASSERT_TRUE(wait_thread_blocked_in_wait_event(thread1_handle), "");

    zx_handle_t event_handle_dup = ZX_HANDLE_INVALID;
    zx_status_t status = zx_handle_duplicate(event_handle, ZX_RIGHT_SAME_RIGHTS, &event_handle_dup);
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_NE(event_handle_dup, ZX_HANDLE_INVALID, "handle duplication failed");
    ASSERT_EQ(zx_handle_close(event_handle), ZX_OK, "handle close failed");

    ASSERT_TRUE(recv_msg(thread1_channel[0], &msg), "Error while receiving msg");
    ASSERT_EQ(msg, (enum message)MSG_WAIT_EVENT_CANCELLED,
              "unexpected reply from thread1 (wait for event)");

    send_msg(thread1_channel[0], MSG_EXIT);
    send_msg(thread2_channel[0], MSG_EXIT);
    EXPECT_EQ(thrd_join(thread1, NULL), thrd_success, "failed to join thread");
    EXPECT_EQ(thrd_join(thread2, NULL), thrd_success, "failed to join thread");
    EXPECT_EQ(zx_handle_close(event_handle_dup), ZX_OK, "handle close failed");
    END_TEST;
}

BEGIN_TEST_CASE(handle_wait_tests)
RUN_TEST(handle_wait_test);
END_TEST_CASE(handle_wait_tests)
