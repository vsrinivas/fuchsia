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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <magenta/syscalls.h>
#include <unittest/test-utils.h>
#include <unittest/unittest.h>

enum handler_kind {
    HANDLER_THREAD,
    HANDLER_PROCESS,
    HANDLER_SYSTEM
};

struct handlers {
    mx_handle_t system;
    mx_handle_t process;
    mx_handle_t thread;
};

enum message {
    MSG_DONE,
    MSG_CRASH,
    MSG_PING,
    MSG_PONG
};

// Test only enabled on supported architectures.
static int for_real = 0;

// Set to zero to disable for debugging purposes.
// TODO(dje): Disabled until debugger API added (we need ability to
// write thread registers).
#define ENABLE_FOR_REAL 0

// Set to non-zero when done, disables watchdog.
static int done_tests = 0;

// Architecture specific ways to crash and then recover from the crash.

static void crash_me(void) {
    unittest_printf("Attempting to crash thread.\n");
#ifdef __x86_64__
    __asm__ volatile("int3");
#endif
    unittest_printf("Thread resuming after crash.\n");
}

static void uncrash_me(mx_handle_t thread) {
    unittest_printf("Attempting to recover from crash.\n");
#ifdef __x86_64__
// TODO(dje): Advance pc by one.
#endif
}

static void send_msg(mx_handle_t handle, enum message msg) {
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    tu_message_write(handle, &data, sizeof(data), NULL, 0, 0);
}

static bool recv_msg(mx_handle_t handle, enum message* msg) {
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    unittest_printf("waiting for message on handle %u\n", handle);

    ASSERT_TRUE(tu_wait_readable(handle), "peer closed while trying to read message");

    tu_message_read(handle, &data, &num_bytes, NULL, 0, 0);
    ASSERT_EQ(num_bytes, sizeof(data), "unexpected message size");

    *msg = data;
    unittest_printf("received message %d\n", *msg);
    return true;
}

static bool resume_thread_from_exception(mx_handle_t thread, mx_handle_t msg_pipe) {
    if (for_real) {
        uncrash_me(thread);
        mx_mark_exception_handled(thread, MX_EXCEPTION_STATUS_RESUME);
    }
    send_msg(msg_pipe, MSG_PING);
    enum message msg;
    ASSERT_TRUE(recv_msg(msg_pipe, &msg), "Error while recieving msg");
    ASSERT_EQ(msg, (enum message)MSG_PONG, "unexpected reply from thread");
    unittest_printf("thread has resumed\n");
    return true;
}

static bool test_received_exception(struct handlers* handlers,
                                    enum handler_kind kind) {
    mx_handle_t handle;
    const char* kind_name;

    if (!for_real)
        return true;

    switch (kind) {
    case HANDLER_THREAD:
        handle = handlers->thread;
        kind_name = "thread";
        break;
    case HANDLER_PROCESS:
        handle = handlers->process;
        kind_name = "process";
        break;
    case HANDLER_SYSTEM:
        handle = handlers->system;
        kind_name = "system";
        break;
    default:
        ASSERT_TRUE(false, "Invalid kind");
    }

    ASSERT_TRUE(tu_wait_readable(handle), "exception handler sender closed");

    mx_exception_report_t report;
    uint32_t num_bytes = sizeof(report);
    tu_message_read(handle, &report, &num_bytes, NULL, 0, 0);
    ASSERT_EQ(num_bytes, sizeof(report), "unexpected message size");

    unittest_printf("exception received from %s handler: pid %u, tid %u\n",
                    kind_name, report.pid, report.tid);
    return true;
}

static void mark_tests_done(mx_handle_t msg_pipe) {
    send_msg(msg_pipe, MSG_DONE);
}

static int thread_func(void* arg) {
    mx_handle_t msg_pipe = (mx_handle_t)(uintptr_t)arg;

    done_tests = 0;
    while (!done_tests) {
        enum message msg;
        ASSERT_TRUE(recv_msg(msg_pipe, &msg), "Error while recieving msg");
        switch (msg) {
        case MSG_DONE:
            done_tests = 1;
            break;
        case MSG_CRASH:
            if (for_real)
                crash_me();
            break;
        case MSG_PING:
            send_msg(msg_pipe, MSG_PONG);
            break;
        default:
            unittest_printf("\nunknown message received: %d\n", msg);
            break;
        }
    }
    mx_thread_exit();
    return 0; // sigh
}

static int watchdog_thread_func(void* arg) {
    for (int i = 0; i < TU_WATCHDOG_DURATION_SECONDS; ++i) {
        mx_nanosleep(1000 * 1000 * 1000);
        if (done_tests)
            mx_thread_exit();
    }
    // This should kill the entire process, not just this thread.
    exit(1);
}

bool exceptions_test(void) {
    BEGIN_TEST;
    mx_status_t status;
    struct handlers send, recv;
    mx_handle_t our_pipe, child_pipe;

#ifdef __x86_64__
    for_real = ENABLE_FOR_REAL;
#endif

    tu_message_pipe_create(&send.system, &recv.system);
    tu_message_pipe_create(&send.process, &recv.process);
    tu_message_pipe_create(&send.thread, &recv.thread);
    tu_message_pipe_create(&our_pipe, &child_pipe);

    mx_handle_t thread_handle =
        tu_thread_create(thread_func, (void*)(uintptr_t)child_pipe, "test-thread");

    mx_handle_t watchdog_thread_handle =
        tu_thread_create(watchdog_thread_func, NULL, "watchdog-thread");
    // We could wait for this thread to exit when we're done, but there's no point.
    (void) watchdog_thread_handle;

    // That's it for test setup, now onto the tests.

    unittest_printf("\nsystem exception handler basic test\n");
    status = mx_set_system_exception_handler(send.system, MX_EXCEPTION_BEHAVIOUR_DEFAULT);
    ASSERT_GE(status, 0, "set_system_exception_handler");

    send_msg(our_pipe, MSG_CRASH);
    ASSERT_TRUE(test_received_exception(&recv, HANDLER_SYSTEM), "");
    ASSERT_TRUE(resume_thread_from_exception(thread_handle, our_pipe), "");

    unittest_printf("\nprocess exception handler basic test\n");
    status = mx_set_exception_handler(0, send.process, MX_EXCEPTION_BEHAVIOUR_DEFAULT);
    ASSERT_GE(status, 0, "set_process_exception_handler");
    send_msg(our_pipe, MSG_CRASH);
    ASSERT_TRUE(test_received_exception(&recv, HANDLER_PROCESS), "");
    ASSERT_TRUE(resume_thread_from_exception(thread_handle, our_pipe), "");

    unittest_printf("\nthread exception handler basic test\n");
    status = mx_set_exception_handler(thread_handle, send.thread, MX_EXCEPTION_BEHAVIOUR_DEFAULT);
    ASSERT_GE(status, 0, "set_thread_exception_handler");

    send_msg(our_pipe, MSG_CRASH);
    ASSERT_TRUE(test_received_exception(&recv, HANDLER_THREAD), "");
    ASSERT_TRUE(resume_thread_from_exception(thread_handle, our_pipe), "");

    mark_tests_done(our_pipe);

    END_TEST;
}

BEGIN_TEST_CASE(exceptions_tests)
RUN_TEST(exceptions_test);
END_TEST_CASE(exceptions_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests();
    return success ? 0 : -1;
}
