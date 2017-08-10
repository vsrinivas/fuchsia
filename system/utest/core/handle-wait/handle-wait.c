// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#include <magenta/compiler.h>

#define ASSERT_NOT_REACHED() \
    assert(0)

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
    mx_handle_t channel;
} thread_data_t;

typedef struct wait_data {
    mx_handle_t handle;
    mx_handle_t signals;
    mx_time_t timeout;
    mx_status_t status;
} wait_data_t;

// [0] is used by main thread
// [1] is used by worker thread
static mx_handle_t thread1_channel[2];
static mx_handle_t thread2_channel[2];

static mx_handle_t event_handle;

// Wait until |handle| is readable or peer is closed (or wait is cancelled).

static bool wait_readable(mx_handle_t handle, enum wait_result* result) {
    mx_signals_t pending;
    mx_signals_t signals = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    mx_time_t deadline = MX_TIME_INFINITE;
    mx_status_t status = mx_object_wait_one(handle, signals, deadline, &pending);
    if (status == MX_ERR_CANCELED) {
        *result = WAIT_CANCELLED;
        return true;
    }
    ASSERT_GE(status, 0, "handle wait one failed");
    if ((pending & MX_CHANNEL_READABLE) != 0) {
        *result = WAIT_READABLE;
        return true;
    }
    unittest_printf("wait_readable: peer closed\n");
    *result = WAIT_CLOSED;
    return true;
}

static bool wait_signaled(mx_handle_t handle, enum wait_result* result) {
    mx_signals_t pending;
    mx_signals_t signals = MX_EVENT_SIGNALED;
    mx_time_t deadline = MX_TIME_INFINITE;
    mx_status_t status = mx_object_wait_one(handle, signals, deadline, &pending);
    if (status == MX_ERR_CANCELED) {
        *result = WAIT_CANCELLED;
        return true;
    }
    ASSERT_GE(status, 0, "handle wait one failed");
    ASSERT_NE(pending & MX_EVENT_SIGNALED, 0u,
              "unexpected return in wait_signaled");
    *result = WAIT_SIGNALED;
    return true;
}

static mx_status_t channel_create(mx_handle_t* handle0, mx_handle_t* handle1) {
    return mx_channel_create(0, handle0, handle1);
}

static bool send_msg(mx_handle_t handle, enum message msg) {
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    mx_status_t status =
        mx_channel_write(handle, 0, &data, sizeof(data), NULL, 0);
    ASSERT_GE(status, 0, "message write failed");
    return true;
}

static bool recv_msg(mx_handle_t handle, enum message* msg) {
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

    ASSERT_GE(mx_channel_read(handle, 0, &data, NULL, num_bytes, 0, &num_bytes, NULL), 0,
              "Error while reading message");
    EXPECT_EQ(num_bytes, sizeof(data), "unexpected message size");
    if (num_bytes != sizeof(data)) {
        mx_thread_exit();
    }
    *msg = (enum message)data;
    unittest_printf("received message %d\n", *msg);
    return true;
}

static bool msg_loop(mx_handle_t channel) {
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
            ASSERT_TRUE(wait_signaled(event_handle, &result), "Error during wait signal call");
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
    mx_signals_t observed;
    data->status = mx_object_wait_one(data->handle, data->signals, mx_deadline_after(data->timeout),
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

    event_handle = MX_HANDLE_INVALID;
    ASSERT_EQ(mx_event_create(0u, &event_handle), 0, "");
    ASSERT_NE(event_handle, MX_HANDLE_INVALID, "event creation failed");

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
    // N.B. We're assuming thread 1 is waiting on event_handle at this point.
    // TODO(vtl): This is a flaky assumption, though the following sleep should help.
    mx_nanosleep(mx_deadline_after(MX_MSEC(20)));

    mx_handle_t event_handle_dup = MX_HANDLE_INVALID;
    mx_status_t status = mx_handle_duplicate(event_handle, MX_RIGHT_SAME_RIGHTS, &event_handle_dup);
    ASSERT_EQ(status, MX_OK, "");
    ASSERT_NE(event_handle_dup, MX_HANDLE_INVALID, "handle duplication failed");
    ASSERT_EQ(mx_handle_close(event_handle), MX_OK, "handle close failed");

    ASSERT_TRUE(recv_msg(thread1_channel[0], &msg), "Error while receiving msg");
    ASSERT_EQ(msg, (enum message)MSG_WAIT_EVENT_CANCELLED,
              "unexpected reply from thread1 (wait for event)");

    send_msg(thread1_channel[0], MSG_EXIT);
    send_msg(thread2_channel[0], MSG_EXIT);
    EXPECT_EQ(thrd_join(thread1, NULL), thrd_success, "failed to join thread");
    EXPECT_EQ(thrd_join(thread2, NULL), thrd_success, "failed to join thread");
    EXPECT_EQ(mx_handle_close(event_handle_dup), MX_OK, "handle close failed");
    END_TEST;
}

BEGIN_TEST_CASE(handle_wait_tests)
RUN_TEST(handle_wait_test);
END_TEST_CASE(handle_wait_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
#endif
