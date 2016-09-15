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
    mx_handle_t pipe;
} thread_data_t;

// [0] is used by main thread
// [1] is used by worker thread
static mx_handle_t thread1_pipe[2];
static mx_handle_t thread2_pipe[2];

static mx_handle_t event_handle;

// Wait until |handle| is readable or peer is closed (or wait is cancelled).

static bool wait_readable(mx_handle_t handle, enum wait_result* result) {
    mx_signals_state_t signals_state;
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    int64_t timeout = MX_TIME_INFINITE;
    mx_status_t status = mx_handle_wait_one(handle, signals, timeout, &signals_state);
    if (status == ERR_HANDLE_CLOSED) {
        *result = WAIT_CANCELLED;
        return true;
    }
    ASSERT_GE(status, 0, "handle wait one failed");
    ASSERT_NEQ(signals_state.satisfied & (MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED), 0u,
               "unexpected return in wait_readable");
    if ((signals_state.satisfied & MX_SIGNAL_READABLE) != 0) {
        *result = WAIT_READABLE;
        return true;
    }
    unittest_printf("wait_readable: peer closed\n");
    *result = WAIT_CLOSED;
    return true;
}

static bool wait_signaled(mx_handle_t handle, enum wait_result* result) {
    mx_signals_state_t signals_state;
    mx_signals_t signals = MX_SIGNAL_SIGNALED;
    int64_t timeout = MX_TIME_INFINITE;
    mx_status_t status = mx_handle_wait_one(handle, signals, timeout, &signals_state);
    if (status == ERR_HANDLE_CLOSED) {
        *result = WAIT_CANCELLED;
        return true;
    }
    ASSERT_GE(status, 0, "handle wait one failed");
    ASSERT_NEQ(signals_state.satisfied & MX_SIGNAL_SIGNALED, 0u,
               "unexpected return in wait_signaled");
    *result = WAIT_SIGNALED;
    return true;
}

static mx_status_t message_pipe_create(mx_handle_t* handle0, mx_handle_t* handle1) {
    mx_handle_t h[2];
    mx_status_t status = mx_msgpipe_create(h, 0);
    *handle0 = h[0];
    *handle1 = h[1];
    return status;
}

static bool send_msg(mx_handle_t handle, enum message msg) {
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    mx_status_t status =
        mx_msgpipe_write(handle, &data, sizeof(data), NULL, 0, 0);
    ASSERT_GE(status, 0, "message write failed");
    return true;
}

static bool recv_msg(mx_handle_t handle, enum message* msg) {
    uint64_t data;

    unittest_printf("waiting for message on handle %u\n", handle);
    enum wait_result result;
    ASSERT_TRUE(wait_readable(handle, &result), "Error during waiting for read call");
    ASSERT_NEQ(result, (enum wait_result)WAIT_CLOSED, "peer closed while trying to read message");
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

    ASSERT_GE(mx_msgpipe_read(handle, &data, &num_bytes, NULL, 0, 0), 0,
              "Error while reading message");
    EXPECT_EQ(num_bytes, sizeof(data), "unexpected message size");
    if (num_bytes != sizeof(data)) {
        mx_thread_exit();
    }
    *msg = (enum message)data;
    unittest_printf("received message %d\n", msg);
    return true;
}

static bool msg_loop(mx_handle_t pipe) {
    bool my_done_tests = false;
    while (!my_done_tests) {
        enum message msg;
        enum wait_result result;
        ASSERT_TRUE(recv_msg(pipe, &msg), "Error while receiving msg");
        switch (msg) {
        case MSG_EXIT:
            my_done_tests = true;
            break;
        case MSG_PING:
            send_msg(pipe, MSG_PONG);
            break;
        case MSG_WAIT_EVENT:
            ASSERT_TRUE(wait_signaled(event_handle, &result), "Error during wait signal call");
            switch (result) {
            case WAIT_SIGNALED:
                send_msg(pipe, MSG_WAIT_EVENT_SIGNALED);
                break;
            case WAIT_CANCELLED:
                send_msg(pipe, MSG_WAIT_EVENT_CANCELLED);
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
    msg_loop(data->pipe);
    unittest_printf("thread %d exiting\n", data->thread_num);
    send_msg(data->pipe, MSG_EXITED);
    return 0;
}

bool handle_wait_test(void) {
    BEGIN_TEST;

    ASSERT_GE(message_pipe_create(&thread1_pipe[0], &thread1_pipe[1]), 0, "pipe creation failed");
    ASSERT_GE(message_pipe_create(&thread2_pipe[0], &thread2_pipe[1]), 0, "pipe creation failed");

    thread_data_t thread1_data = {1, thread1_pipe[1]};
    thread_data_t thread2_data = {2, thread2_pipe[1]};

    thrd_t thread1;
    ASSERT_EQ(thrd_create(&thread1, worker_thread_func, &thread1_data), thrd_success,
              "thread creation failed");
    thrd_t thread2;
    ASSERT_EQ(thrd_create(&thread2, worker_thread_func, &thread2_data), thrd_success,
              "thread creation failed");
    unittest_printf("threads started\n");

    event_handle = mx_event_create(0u);
    ASSERT_GT(event_handle, 0, "event creation failed");

    enum message msg;
    send_msg(thread1_pipe[0], MSG_PING);
    ASSERT_TRUE(recv_msg(thread1_pipe[0], &msg), "Error while receiving msg");
    EXPECT_EQ(msg, (enum message)MSG_PONG, "unexpected reply to ping1");

    send_msg(thread1_pipe[0], MSG_WAIT_EVENT);

    send_msg(thread2_pipe[0], MSG_PING);
    ASSERT_TRUE(recv_msg(thread2_pipe[0], &msg), "Error while receiving msg");
    EXPECT_EQ(msg, (enum message)MSG_PONG, "unexpected reply to ping2");

    // Verify thread 1 is woken up when we close the handle it's waiting on
    // when there exists a duplicate of the handle.
    // N.B. We're assuming thread 1 is waiting on event_handle at this point.
    // TODO(vtl): This is a flaky assumption, though the following sleep should help.
    mx_nanosleep(MX_MSEC(20));

    mx_handle_t event_handle_dup = mx_handle_duplicate(event_handle, MX_RIGHT_SAME_RIGHTS);
    ASSERT_GE(event_handle_dup, 0, "handle duplication failed");
    ASSERT_EQ(mx_handle_close(event_handle), NO_ERROR, "handle close failed");

    ASSERT_TRUE(recv_msg(thread1_pipe[0], &msg), "Error while receiving msg");
    ASSERT_EQ(msg, (enum message)MSG_WAIT_EVENT_CANCELLED,
              "unexpected reply from thread1 (wait for event)");

    send_msg(thread1_pipe[0], MSG_EXIT);
    send_msg(thread2_pipe[0], MSG_EXIT);
    EXPECT_EQ(thrd_join(thread1, NULL), thrd_success, "failed to join thread");
    EXPECT_EQ(thrd_join(thread2, NULL), thrd_success, "failed to join thread");
    EXPECT_EQ(mx_handle_close(event_handle_dup), NO_ERROR, "handle close failed");
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
