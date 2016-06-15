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

#define WATCHDOG_DURATION_SECONDS 2
#define WATCHDOG_DURATION_NANOSECONDS ((int64_t) WATCHDOG_DURATION_SECONDS * 1000 * 1000 * 1000)

enum handler_kind
{
    HANDLER_THREAD,
    HANDLER_PROCESS,
    HANDLER_SYSTEM
};

struct handlers
{
    mx_handle_t system;
    mx_handle_t process;
    mx_handle_t thread;
};

enum message
{
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

static void syscall_fail(const char *name, mx_status_t status)
{
    printf("syscall %s failed, rc %d\n", name, status);
    exit(1);
}

static mx_status_t my_create_message_pipe(mx_handle_t* handle0, mx_handle_t* handle1)
{
    mx_handle_t status = _magenta_message_pipe_create(handle1);
    if (status < 0) return status;
    *handle0 = status;
    return NO_ERROR;
}

typedef int (*thread_start_func)(void*);

static mx_status_t my_thread_create(thread_start_func entry, void* arg,
                                    mx_handle_t* out_handle, const char* name)
{
    if (!name) name = "";
    mx_handle_t status = _magenta_thread_create(entry, arg, name, strlen(name) + 1);
    if (status < 0) return status;
    *out_handle = status;
    return NO_ERROR;
}

static mx_status_t my_wait(const mx_handle_t* handles, const mx_signals_t* signals,
                           uint32_t num_handles, uint32_t* result_index,
                           mx_time_t deadline, //xyzdje, unused in _magenta_wait
                           mx_signals_t* satisfied_signals,
                           mx_signals_t* satisfiable_signals)
{
    mx_status_t result;

    if (num_handles == 1u) {
        result =
            _magenta_handle_wait_one(*handles, *signals, MX_TIME_INFINITE,
                                     satisfied_signals, satisfiable_signals);
    } else {
        result = _magenta_handle_wait_many(num_handles, handles, signals, MX_TIME_INFINITE,
                                           satisfied_signals, satisfiable_signals);
    }

    // xyzdje, from _magenta_wait: TODO(cpu): implement |result_index|, see MG-33 bug.
    return result;
}

static mx_status_t my_write_message(mx_handle_t handle, const void* bytes, uint32_t num_bytes,
                                    const mx_handle_t* handles, uint32_t num_handles,
                                    uint32_t flags)
{
    return _magenta_message_write(handle, bytes, num_bytes, handles, num_handles, flags);
}

static mx_status_t my_read_message(mx_handle_t handle, void* bytes, uint32_t* num_bytes,
                                   mx_handle_t* handles, uint32_t* num_handles, uint32_t flags)
{
    return _magenta_message_read(handle, bytes, num_bytes, handles, num_handles, flags);
}

// Architecture specific ways to crash and then recover from the crash.

static void crash_me(void)
{
    printf("Attempting to crash thread.\n");
#ifdef __x86_64__
    __asm__ volatile ("int3");
#endif
    printf("Thread resuming after crash.\n");
}

static void uncrash_me(mx_handle_t thread)
{
    printf("Attempting to recover from crash.\n");
#ifdef __x86_64__
    // TODO(dje): Advance pc by one.
#endif
}

// Wait until |handle| is readable or peer is closed.
// Result is true if readable, otherwise false.

static bool wait_handle(mx_handle_t handle)
{
    mx_signals_t satisfied_signals, satisfiable_signals;
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    mx_status_t result = my_wait(&handle, &signals, 1, NULL, WATCHDOG_DURATION_NANOSECONDS,
                                 &satisfied_signals, &satisfiable_signals);
    if (result != NO_ERROR)
        syscall_fail("my_wait returned %d\n", result);
    if ((satisfied_signals & MX_SIGNAL_READABLE) == 0) {
        printf("my_wait: peer closed\n");
        return false;
    }
    return true;
}

static void send_msg(mx_handle_t handle, enum message msg)
{
    uint64_t data = msg;
    printf("sending message %d on handle %u\n", msg, handle);
    mx_status_t status = my_write_message(handle, &data, sizeof(data), NULL, 0, 0);
    if (status != NO_ERROR)
        syscall_fail("my_write_message", status);
}

static enum message recv_msg(mx_handle_t handle)
{
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    printf("waiting for message on handle %u\n", handle);

    if (!wait_handle(handle))
    {
        printf("peer closed while trying to read message\n");
        exit (1);
    }
    mx_status_t status = my_read_message(handle, &data, &num_bytes, NULL, 0, 0);
    if (status != NO_ERROR)
        syscall_fail("my_read_message", status);
    if (num_bytes != sizeof(data))
    {
        printf("unexpected message size: %u\n", num_bytes);
        exit(1);
    }
    printf("received message %d\n", (enum message) data);
    return (enum message) data;
}

static void resume_thread_from_exception(mx_handle_t thread, mx_handle_t msg_pipe)
{
    if (for_real)
    {
        uncrash_me(thread);
        _magenta_mark_exception_handled(thread, MX_EXCEPTION_STATUS_RESUME);
    }
    send_msg(msg_pipe, MSG_PING);
    enum message msg = recv_msg(msg_pipe);
    if (msg != MSG_PONG)
    {
        printf("unexpected reply from thread: %d\n", msg);
        exit(1);
    }
    printf("thread has resumed\n");
}

static void test_received_exception(struct handlers* handlers,
                                    enum handler_kind kind)
{
    mx_handle_t handle;
    const char* kind_name;

    if (!for_real)
        return;

    switch (kind)
    {
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
        abort();
    }

    if (!wait_handle(handle))
    {
        printf("exception handler sender closed\n");
        exit (1);
    }

    mx_exception_report_t report;
    uint32_t num_bytes = sizeof(report);
    mx_status_t status = my_read_message(handle, &report, &num_bytes, NULL, 0, 0);
    if (status != NO_ERROR)
        syscall_fail("my_read_message of exception report", status);
    if (num_bytes != sizeof(report))
    {
        printf("unexpected message size: %u\n", num_bytes);
        exit(1);
    }

    printf("exception received from %s handler: pid %u, tid %u\n",
           kind_name, report.pid, report.tid);
}

static void mark_tests_done(mx_handle_t msg_pipe)
{
    send_msg(msg_pipe, MSG_DONE);
}

static int thread_func(void* arg)
{
    mx_handle_t msg_pipe = (mx_handle_t) (uintptr_t) arg;

    done_tests = 0;
    while (!done_tests)
    {
        enum message msg = recv_msg(msg_pipe);
        switch (msg)
        {
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
            printf("\nunknown message received: %d\n", msg);
            break;
        }
    }
    _magenta_thread_exit();
    return 0; // sigh
}

static int watchdog_thread_func(void* arg)
{
    for (int i = 0; i < WATCHDOG_DURATION_SECONDS; ++i)
    {
        _magenta_nanosleep(1000 * 1000 * 1000);
        if (done_tests)
            _magenta_thread_exit();
    }
    // This should kill the entire process, not just this thread.
    exit (1);
}

int main(void)
{
    mx_status_t status;
    struct handlers send,recv;
    mx_handle_t our_pipe, child_pipe;

#ifdef __x86_64__
    for_real = ENABLE_FOR_REAL;
#endif

    status = my_create_message_pipe(&send.system, &recv.system);
    if (status < 0)
        syscall_fail("system exception pipe", status);

    status = my_create_message_pipe(&send.process, &recv.process);
    if (status < 0)
        syscall_fail("process exception pipe", status);

    status = my_create_message_pipe(&send.thread, &recv.thread);
    if (status < 0)
        syscall_fail("thread exception pipe", status);

    status = my_create_message_pipe(&our_pipe, &child_pipe);
    if (status < 0)
        syscall_fail("parent/child pipe", status);

    mx_handle_t thread_handle;
    status = my_thread_create(thread_func, (void*) (uintptr_t) child_pipe, &thread_handle, "test-thread");
    if (status < 0)
        syscall_fail("my_thread_create", status);

    mx_handle_t watchdog_thread_handle;
    status = my_thread_create(watchdog_thread_func, NULL, &watchdog_thread_handle, "watchdog-thread");
    if (status < 0)
        syscall_fail("my_thread_create, watchdog", status);

    // That's it for test setup, now onto the tests.

    printf("\nsystem exception handler basic test\n");
    status = _magenta_set_system_exception_handler(send.system, MX_EXCEPTION_BEHAVIOUR_DEFAULT);
    if (status < 0)
        syscall_fail("set_system_exception_handler", status);
    send_msg(our_pipe, MSG_CRASH);
    test_received_exception(&recv, HANDLER_SYSTEM);
    resume_thread_from_exception(thread_handle, our_pipe);

    printf("\nprocess exception handler basic test\n");
    status = _magenta_set_exception_handler(0, send.process, MX_EXCEPTION_BEHAVIOUR_DEFAULT);
    if (status < 0)
        syscall_fail("set_process_exception_handler", status);
    send_msg(our_pipe, MSG_CRASH);
    test_received_exception(&recv, HANDLER_PROCESS);
    resume_thread_from_exception(thread_handle, our_pipe);

    printf("\nthread exception handler basic test\n");
    status = _magenta_set_exception_handler(thread_handle, send.thread, MX_EXCEPTION_BEHAVIOUR_DEFAULT);
    if (status < 0)
        syscall_fail("set_thread_exception_handler", status);
    send_msg(our_pipe, MSG_CRASH);
    test_received_exception(&recv, HANDLER_THREAD);
    resume_thread_from_exception(thread_handle, our_pipe);

    printf("done\n");
    mark_tests_done(our_pipe);

    return 0;
}
