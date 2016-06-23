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
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>

#include <runtime/compiler.h>

typedef int (*thread_start_func_t)(void*);

#define ASSERT_NOT_REACHED() \
    assert(0)

enum message
{
    MSG_EXIT,
    MSG_EXITED,
    MSG_WAIT_THREAD2,
    MSG_WAIT_THREAD2_SIGNALLED,
    MSG_WAIT_THREAD2_CANCELLED,
    MSG_PING,
    MSG_PONG,
    MSG_READ_CANCELLED,
};

enum wait_result
{
    WAIT_READABLE,
    WAIT_SIGNALLED,
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

static mx_handle_t thread1_handle;
static mx_handle_t thread2_handle;

static void log_msg(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    vprintf(msg, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}

// This function exists for debugging purposes.

static void my_exit(int rc) __NO_RETURN;
static void my_exit(int rc)
{
    exit(rc);
}

static void syscall_fail(const char *name, mx_status_t status)
{
    // TODO(dje): Get reason string.
    log_msg("syscall %s failed, rc %d", name, status);
    my_exit(10);
}

static mx_handle_t thread_create(thread_start_func_t entry, void* arg,
                                 const char* name)
{
    if (!name)
        name = "";
    mx_handle_t handle = _magenta_thread_create(entry, arg, name, strlen(name) + 1);
    if (handle < 0)
        syscall_fail("thread_create", handle);
    log_msg("created thread, handle %d", handle);
    return handle;
}

// Wait until |handle| is readable or peer is closed (or wait is cancelled).

static enum wait_result wait_readable(mx_handle_t handle)
{
    mx_signals_t satisfied_signals, satisfiable_signals;
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    int64_t timeout = MX_TIME_INFINITE;
    mx_status_t status = _magenta_handle_wait_one(handle, signals, timeout,
                                                  &satisfied_signals, &satisfiable_signals);
    if (status == ERR_CANCELLED)
        return WAIT_CANCELLED;
    if (status < 0)
        syscall_fail("handle_wait_one", status);
    if ((satisfied_signals & (MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED)) == 0) {
        log_msg("unexpected return in wait_readable");
        my_exit(14);
    }
    if ((satisfied_signals & MX_SIGNAL_READABLE) != 0)
        return WAIT_READABLE;
    log_msg("wait_readable: peer closed");
    return WAIT_CLOSED;
}

static enum wait_result wait_signalled(mx_handle_t handle)
{
    mx_signals_t satisfied_signals, satisfiable_signals;
    mx_signals_t signals = MX_SIGNAL_SIGNALED;
    int64_t timeout = MX_TIME_INFINITE;
    mx_status_t status = _magenta_handle_wait_one(handle, signals, timeout,
                                                  &satisfied_signals, &satisfiable_signals);
    if (status == ERR_CANCELLED)
        return WAIT_CANCELLED;
    if (status < 0)
        syscall_fail("wait_signalled", status);
    if ((satisfied_signals & MX_SIGNAL_SIGNALED) == 0) {
        log_msg("unexpected return in wait_signalled");
        my_exit(15);
    }
    return WAIT_SIGNALLED;
}

static void message_pipe_create(mx_handle_t* handle0, mx_handle_t* handle1)
{
    mx_handle_t h0 = _magenta_message_pipe_create(handle1);
    if (h0 < 0)
        syscall_fail("parent/child pipe", h0);
    *handle0 = h0;
}

static void message_write(mx_handle_t handle, const void* bytes, uint32_t num_bytes)
{
    mx_status_t status =
        _magenta_message_write(handle, bytes, num_bytes, NULL, 0, 0);
    if (status < 0)
        syscall_fail("message_write", status);
}

static void message_read(mx_handle_t handle, void* bytes, uint32_t* num_bytes)
{
    mx_status_t status =
        _magenta_message_read(handle, bytes, num_bytes, NULL, 0, 0);
    if (status < 0)
        syscall_fail("message_read", status);
}

static mx_handle_t handle_duplicate(mx_handle_t handle)
{
    mx_handle_t h = _magenta_handle_duplicate(handle);
    if (h < 0)
        syscall_fail("handle_duplicate", h);
    return h;
}

static void send_msg(mx_handle_t handle, enum message msg)
{
    uint64_t data = msg;
    log_msg("sending message %d on handle %u", msg, handle);
    message_write(handle, &data, sizeof(data));
}

static enum message recv_msg(mx_handle_t handle)
{
    uint64_t data;

    log_msg("waiting for message on handle %u", handle);

    switch (wait_readable(handle))
    {
    case WAIT_READABLE:
        break;
    case WAIT_CLOSED:
        log_msg("peer closed while trying to read message");
        my_exit(16);
    case WAIT_CANCELLED:
        log_msg("read wait cancelled");
        return MSG_READ_CANCELLED;
    default:
        ASSERT_NOT_REACHED();
    }

    uint32_t num_bytes = sizeof(data);
    message_read(handle, &data, &num_bytes);
    if (num_bytes != sizeof(data))
    {
        log_msg("unexpected message size: %u", num_bytes);
        _magenta_thread_exit();
    }

    log_msg("received message %d", (enum message) data);
    return (enum message) data;
}

static void msg_loop(mx_handle_t pipe)
{
    bool my_done_tests = false;
    while (!my_done_tests)
    {
        enum message msg = recv_msg(pipe);
        switch (msg)
        {
        case MSG_EXIT:
            my_done_tests = true;
            break;
        case MSG_PING:
            send_msg(pipe, MSG_PONG);
            break;
        case MSG_WAIT_THREAD2:
            switch (wait_signalled(thread2_handle))
            {
            case WAIT_SIGNALLED:
                send_msg(pipe, MSG_WAIT_THREAD2_SIGNALLED);
                break;
            case WAIT_CANCELLED:
                send_msg(pipe, MSG_WAIT_THREAD2_CANCELLED);
                break;
            default:
                ASSERT_NOT_REACHED();
            }
            break;
        default:
            log_msg("unknown message received: %d", msg);
            break;
        }
    }
}

static int worker_thread_func(void* arg) {
    thread_data_t* data = arg;
    msg_loop(data->pipe);
    log_msg("thread %d exiting", data->thread_num);
    send_msg(data->pipe, MSG_EXITED);
    _magenta_thread_exit();
}

int main(void) {
    message_pipe_create(&thread1_pipe[0], &thread1_pipe[1]);
    message_pipe_create(&thread2_pipe[0], &thread2_pipe[1]);

    thread_data_t thread1_data = { 1, thread1_pipe[1] };
    thread_data_t thread2_data = { 2, thread2_pipe[1] };

    thread1_handle = thread_create(worker_thread_func, (void*) &thread1_data, "thread1");
    thread2_handle = thread_create(worker_thread_func, (void*) &thread2_data, "thread2");
    log_msg("threads started");

    enum message msg;
    send_msg(thread1_pipe[0], MSG_PING);
    msg = recv_msg(thread1_pipe[0]);
    if (msg != MSG_PONG)
        log_msg("unexpected reply to ping1: %d", msg);

    send_msg(thread1_pipe[0], MSG_WAIT_THREAD2);

    send_msg(thread2_pipe[0], MSG_PING);
    msg = recv_msg(thread2_pipe[0]);
    if (msg != MSG_PONG)
        log_msg("unexpected reply to ping2: %d", msg);

    // Verify thread 1 is woken up when we close the handle it's waiting on
    // when there exists a duplicate of the handle.
    // N.B. We're assuming thread 1 is waiting on thread 2 at this point.

    mx_handle_t thread2_handle_dup = handle_duplicate(thread2_handle);
    _magenta_handle_close(thread2_handle);

    msg = recv_msg(thread1_pipe[0]);
    if (msg != MSG_WAIT_THREAD2_CANCELLED) {
        log_msg("unexpected reply from thread1 (wait for thread2)");
        exit(20);
    }

    send_msg(thread1_pipe[0], MSG_EXIT);
    send_msg(thread2_pipe[0], MSG_EXIT);
    wait_signalled(thread1_handle);
    wait_signalled(thread2_handle_dup);

    log_msg("Success");
    return 0;
}
