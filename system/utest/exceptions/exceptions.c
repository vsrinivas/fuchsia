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
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WATCHDOG_DURATION_SECONDS 2
#define WATCHDOG_DURATION_NANOSECONDS ((int64_t)WATCHDOG_DURATION_SECONDS * 1000 * 1000 * 1000)

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

static mx_status_t my_create_message_pipe(mx_handle_t* handle0, mx_handle_t* handle1) {
    mx_handle_t status = mx_message_pipe_create(handle1);
    if (status < 0)
        return status;
    *handle0 = status;
    return NO_ERROR;
}

typedef int (*thread_start_func)(void*);

static mx_status_t my_thread_create(thread_start_func entry, void* arg,
                                    mx_handle_t* out_handle, const char* name) {
    if (!name)
        name = "";
    mx_handle_t status = mx_thread_create(entry, arg, name, strlen(name) + 1);
    if (status < 0)
        return status;
    *out_handle = status;
    return NO_ERROR;
}

static mx_status_t my_wait(const mx_handle_t* handles, const mx_signals_t* signals,
                           uint32_t num_handles, uint32_t* result_index,
                           mx_time_t deadline, //xyzdje, unused in _magenta_wait
                           mx_signals_t* satisfied_signals,
                           mx_signals_t* satisfiable_signals) {
    mx_status_t result;

    if (num_handles == 1u) {
        result =
            mx_handle_wait_one(*handles, *signals, MX_TIME_INFINITE,
                                     satisfied_signals, satisfiable_signals);
    } else {
        result = mx_handle_wait_many(num_handles, handles, signals, MX_TIME_INFINITE,
                                           satisfied_signals, satisfiable_signals);
    }

    // xyzdje, from _magenta_wait: TODO(cpu): implement |result_index|, see MG-33 bug.
    return result;
}

static mx_status_t my_write_message(mx_handle_t handle, const void* bytes, uint32_t num_bytes,
                                    const mx_handle_t* handles, uint32_t num_handles,
                                    uint32_t flags) {
    return mx_message_write(handle, bytes, num_bytes, handles, num_handles, flags);
}

static mx_status_t my_read_message(mx_handle_t handle, void* bytes, uint32_t* num_bytes,
                                   mx_handle_t* handles, uint32_t* num_handles, uint32_t flags) {
    return mx_message_read(handle, bytes, num_bytes, handles, num_handles, flags);
}

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

// Wait until |handle| is readable or peer is closed.
// Result is true if readable, otherwise false.

static bool wait_handle(mx_handle_t handle) {
    mx_signals_t satisfied_signals, satisfiable_signals;
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    mx_status_t result = my_wait(&handle, &signals, 1, NULL, WATCHDOG_DURATION_NANOSECONDS,
                                 &satisfied_signals, &satisfiable_signals);
    ASSERT_EQ(result, NO_ERROR, "my_wait failed");
    ASSERT_NEQ(satisfied_signals & MX_SIGNAL_READABLE, 0u, "my_wait: peer closed");

    return true;
}

static mx_status_t send_msg(mx_handle_t handle, enum message msg) {
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    mx_status_t status = my_write_message(handle, &data, sizeof(data), NULL, 0, 0);
    return status;
}

static bool recv_msg(mx_handle_t handle, enum message* msg) {
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    unittest_printf("waiting for message on handle %u\n", handle);

    ASSERT_TRUE(wait_handle(handle), "peer closed while trying to read message");

    mx_status_t status = my_read_message(handle, &data, &num_bytes, NULL, 0, 0);
    ASSERT_EQ(status, NO_ERROR, "my_read_message failed");
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
    ASSERT_EQ(send_msg(msg_pipe, MSG_PING), NO_ERROR, "send message failed");
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

    ASSERT_TRUE(wait_handle(handle), "exception handler sender closed");

    mx_exception_report_t report;
    uint32_t num_bytes = sizeof(report);
    mx_status_t status = my_read_message(handle, &report, &num_bytes, NULL, 0, 0);
    ASSERT_EQ(status, NO_ERROR, "my_read_message of exception report");
    ASSERT_EQ(num_bytes, sizeof(report), "unexpected message size");

    unittest_printf("exception received from %s handler: pid %u, tid %u\n",
                    kind_name, report.pid, report.tid);
    return true;
}

static void mark_tests_done(mx_handle_t msg_pipe) {
    EXPECT_EQ(send_msg(msg_pipe, MSG_DONE), NO_ERROR, "send message failed");
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
            ASSERT_EQ(send_msg(msg_pipe, MSG_PONG), NO_ERROR, "send message failed");
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
    for (int i = 0; i < WATCHDOG_DURATION_SECONDS; ++i) {
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

    status = my_create_message_pipe(&send.system, &recv.system);
    ASSERT_GE(status, 0, "system exception pipe");

    status = my_create_message_pipe(&send.process, &recv.process);
    ASSERT_GE(status, 0, "process exception pipe");

    status = my_create_message_pipe(&send.thread, &recv.thread);
    ASSERT_GE(status, 0, "thread exception pipe");

    status = my_create_message_pipe(&our_pipe, &child_pipe);
    ASSERT_GE(status, 0, "parent/child pipe");

    mx_handle_t thread_handle;
    status = my_thread_create(thread_func, (void*)(uintptr_t)child_pipe, &thread_handle, "test-thread");
    ASSERT_GE(status, 0, "my_thread_create");

    mx_handle_t watchdog_thread_handle;
    status = my_thread_create(watchdog_thread_func, NULL, &watchdog_thread_handle, "watchdog-thread");
    ASSERT_GE(status, 0, "my_thread_create, watchdog");

    // That's it for test setup, now onto the tests.

    unittest_printf("\nsystem exception handler basic test\n");
    status = mx_set_system_exception_handler(send.system, MX_EXCEPTION_BEHAVIOUR_DEFAULT);
    ASSERT_GE(status, 0, "set_system_exception_handler");

    ASSERT_EQ(send_msg(our_pipe, MSG_CRASH), NO_ERROR, "send message failed");
    ASSERT_TRUE(test_received_exception(&recv, HANDLER_SYSTEM), "");
    ASSERT_TRUE(resume_thread_from_exception(thread_handle, our_pipe), "");

    unittest_printf("\nprocess exception handler basic test\n");
    status = mx_set_exception_handler(0, send.process, MX_EXCEPTION_BEHAVIOUR_DEFAULT);
    ASSERT_GE(status, 0, "set_process_exception_handler");
    ASSERT_EQ(send_msg(our_pipe, MSG_CRASH), NO_ERROR, "send message failed");
    ASSERT_TRUE(test_received_exception(&recv, HANDLER_PROCESS), "");
    ASSERT_TRUE(resume_thread_from_exception(thread_handle, our_pipe), "");

    unittest_printf("\nthread exception handler basic test\n");
    status = mx_set_exception_handler(thread_handle, send.thread, MX_EXCEPTION_BEHAVIOUR_DEFAULT);
    ASSERT_GE(status, 0, "set_thread_exception_handler");

    ASSERT_EQ(send_msg(our_pipe, MSG_CRASH), NO_ERROR, "send message failed");
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
