// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-handler.h"

#include <inttypes.h>

#include <magenta/process.h>
#include <magenta/status.h>
#include <magenta/syscalls/port.h>
#include <magenta/syscalls/exception.h>
#include <magenta/threads.h>

#define EXCEPTION_PORT_KEY   1
#define TEST_ENDED_EVENT_KEY 2

typedef struct handler_data {
    // For listening for test exceptions and test completion.
    mx_handle_t port;
    // List of all processes expected to crash.
    crash_list_t crash_list;
} handler_data_t;

/**
 * Kills the crashing process if it is in the registered list. Processes are
 * registered in tests via REGISTER_CRASH if a crash is expected.
 *
 * If killing failed, the test will be terminated.
 *
 * If the crash was not registered, it will be bubbled up to the crashlogger,
 * and then the test will be terminated.
 */
static void process_exception(crash_list_t crash_list,
                              const mx_packet_exception_t* exception) {
    // Check if the crashed process is in the registered list and remove
    // it if so.
    mx_handle_t process = crash_list_delete_koid(crash_list, exception->pid);

    // The crashing process was not registered to crash. We should
    // let crashlogger print out the details and then fail the test.
    if (process == MX_HANDLE_INVALID) {
        UNITTEST_TRACEF(
            "FATAL: process [%" PRIu64 "] crashed but was not registered\n",
            exception->pid);
        mx_handle_t process;
        mx_status_t status = mx_object_get_child(MX_HANDLE_INVALID,
                                                 exception->pid,
                                                 MX_RIGHT_SAME_RIGHTS,
                                                 &process);
        if (status != MX_OK) {
            UNITTEST_TRACEF(
                "FATAL: failed to get a handle to [%" PRIu64 "] : error %s\n",
                exception->pid, mx_status_get_string(status));
            exit(MX_ERR_INTERNAL);
        }
        mx_handle_t thread;
        status = mx_object_get_child(process, exception->tid,
                                     MX_RIGHT_SAME_RIGHTS, &thread);
        if (status != MX_OK) {
            UNITTEST_TRACEF(
                "FATAL: failed to get a handle to [%" PRIu64 ".%" PRIu64 "] : error %s\n",
                exception->pid, exception->tid, mx_status_get_string(status));
            mx_handle_close(process);
            exit(MX_ERR_INTERNAL);
        }
        // Pass the exception up to crashlogger.
        status = mx_task_resume(thread,
                                MX_RESUME_EXCEPTION | MX_RESUME_TRY_NEXT);
        if (status == MX_OK) {
            // Give crashlogger a little time to print info about the crashed
            // thread.
            mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
        } else {
            UNITTEST_TRACEF(
                "FATAL: could not pass exception from [%" PRIu64 ".%" PRIu64 "] : error %s\n",
                exception->pid, exception->tid, mx_status_get_string(status));
        }
        // This may not be reached if the test process itself crashed,
        // as crashlogger will kill the crashed process.
        mx_handle_close(process);
        mx_handle_close(thread);
        // TODO: fail the test more gracefully.
        exit(MX_ERR_INTERNAL);
    }
    mx_status_t status = mx_task_kill(process);
    if (status != MX_OK) {
        UNITTEST_TRACEF(
            "FATAL: failed to kill process [%" PRIu64 "]\n", exception->pid);
        exit(MX_ERR_INTERNAL);
    }
    mx_handle_close(process);
}

static int run_crash_handler(void* arg) {
    handler_data_t* data = (handler_data_t*)arg;
    mx_port_packet_t packet;
    while (true) {
        mx_status_t status = mx_port_wait(data->port, MX_TIME_INFINITE,
                                          &packet, 0);
        if (status != MX_OK) {
            exit(MX_ERR_INTERNAL);
        }
        switch(packet.key) {
        case EXCEPTION_PORT_KEY:
            process_exception(data->crash_list, &packet.exception);
            break;
        case TEST_ENDED_EVENT_KEY:
            return 0;
        }
    }
    __UNREACHABLE;
}

bool run_test_with_crash_handler(struct test_info* current_test_info,
                                 bool (*test_to_run)(void)) {
    mx_handle_t port;
    mx_status_t status = mx_port_create(0, &port);
    if (status != MX_OK) {
        UNITTEST_TRACEF("failed to create port: error %s\n",
            mx_status_get_string(status));
        return false;
    }
    // TODO: replace with job level exception handling.
    status = mx_task_bind_exception_port(mx_process_self(), port,
                                         EXCEPTION_PORT_KEY, 0);
    if (status != MX_OK) {
        UNITTEST_TRACEF("failed to bind to exception port: error %s\n",
            mx_status_get_string(status));
        mx_handle_close(port);
        return false;
    }
    mx_handle_t test_ended_event;
    status = mx_event_create(0, &test_ended_event);
    if (status != MX_OK) {
        UNITTEST_TRACEF("failed to create event: error %s\n",
            mx_status_get_string(status));
        mx_handle_close(port);
        return false;
    }
    status = mx_object_wait_async(test_ended_event, port, TEST_ENDED_EVENT_KEY,
                                  MX_USER_SIGNAL_0, MX_WAIT_ASYNC_ONCE);
    if (status != MX_OK) {
        UNITTEST_TRACEF(
            "failed to wait on test_ended_event: error %s\n",
            mx_status_get_string(status));
        mx_handle_close(port);
        mx_handle_close(test_ended_event);
        return false;
    }
    // Wait for crashes on a separate thread.
    thrd_t crash_handler;
    handler_data_t handler_data = {
        .port = port,
        .crash_list = current_test_info->crash_list
    };
    int thrd_res = thrd_create(&crash_handler, run_crash_handler,
                               (void*)&handler_data);
    if (thrd_res != thrd_success) {
        UNITTEST_TRACEF("failed to create crash handler thread\n");
        mx_handle_close(port);
        mx_handle_close(test_ended_event);
        return false;
    }
    bool test_result = test_to_run();

    // Notify the crash handler of test completion so it can exit gracefully.
    status = mx_object_signal(test_ended_event, 0, MX_USER_SIGNAL_0);
    if (status == MX_OK) {
        thrd_res = thrd_join(crash_handler, NULL);
    }
    mx_handle_close(port);
    mx_handle_close(test_ended_event);

    ASSERT_EQ(status, MX_OK, "");
    ASSERT_EQ(thrd_res, thrd_success, "");
    ASSERT_TRUE(test_result, "");
    return true;
}
