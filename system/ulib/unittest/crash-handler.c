// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-handler.h"

#include <inttypes.h>

#include <magenta/process.h>
#include <magenta/status.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/port.h>
#include <magenta/threads.h>

#define EXCEPTION_PORT_KEY 1
// The test completed without the test thread crashing.
#define TEST_ENDED_EVENT_KEY 2
// The test thread had a registered crash.
#define TEST_THREAD_TERMINATED_KEY 3

// Signals sent from the test thread to the crash handler port to indicate
// the test result.
#define TEST_PASSED_SIGNAL MX_USER_SIGNAL_0
#define TEST_FAILED_SIGNAL MX_USER_SIGNAL_1

/**
 * Kills the crashing process or thread found in the registered list matching
 * the exception report. Processes or threads are registered in tests via
 * REGISTER_CRASH if a crash is expected.
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
    mx_handle_t match = crash_list_delete_koid(crash_list, exception->pid);
    if (match == MX_HANDLE_INVALID) {
        // The test may have registered a thread handle instead.
        match = crash_list_delete_koid(crash_list, exception->tid);
    }

    // The crash was not registered. We should let crashlogger print out the
    // details and then fail the test.
    if (match == MX_HANDLE_INVALID) {
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
    mx_status_t status = mx_task_kill(match);
    if (status != MX_OK) {
        UNITTEST_TRACEF(
            "FATAL: failed to kill [%" PRIu64 ".%" PRIu64 "]  : error %s\n",
            exception->pid, exception->tid, mx_status_get_string(status));
        exit(MX_ERR_INTERNAL);
    }

    // The exception is still unprocessed. We should wait for termination so
    // there is no race condition with when we unbind the exception port.
    status = mx_object_wait_one(match, MX_TASK_TERMINATED, MX_TIME_INFINITE, NULL);
    if (status != MX_OK) {
        UNITTEST_TRACEF("FATAL: failed to wait for termination  : error %s\n",
                        mx_status_get_string(status));
        exit(MX_ERR_INTERNAL);
    }
    mx_handle_close(match);
}

// Returns the test result if it completes, else true if the test thread
// had a registered crash.
static test_result_t watch_test_thread(mx_handle_t port, crash_list_t crash_list) {
    mx_port_packet_t packet;
    while (true) {
        mx_status_t status = mx_port_wait(port, MX_TIME_INFINITE, &packet, 0);
        if (status != MX_OK) {
            UNITTEST_TRACEF("failed to wait on port: error %s\n",
                            mx_status_get_string(status));
            exit(MX_ERR_INTERNAL);
        }
        switch (packet.key) {
        case EXCEPTION_PORT_KEY:
            process_exception(crash_list, &packet.exception);
            break;
        case TEST_ENDED_EVENT_KEY:
            if (packet.signal.observed & TEST_PASSED_SIGNAL) {
                return TEST_PASSED;
            } else if (packet.signal.observed & TEST_FAILED_SIGNAL) {
                return TEST_FAILED;
            } else {
                UNITTEST_TRACEF("unknown test ended event signal: %u\n",
                                packet.signal.observed);
                exit(MX_ERR_INTERNAL);
            }
        case TEST_THREAD_TERMINATED_KEY:
            // The test thread exited without sending the
            // TEST_ENDED_EVENT_KEY packet, so we must have killed the crashing
            // thread. If it was an unregistered crash, we would have exited
            // and failed the test already, so this must be a registered crash.
            return TEST_CRASHED;
        }
    }
    __UNREACHABLE;
}

typedef struct test_data {
    // The test function to call.
    bool (*test_function)(void*);
    void* test_function_arg;

    // For signaling TEST_PASSED_SIGNAL or TEST_FAILED_SIGNAL.
    mx_handle_t test_ended_event;
    // For registering test termination.
    mx_handle_t port;

    // For registering the test thread, if it is expected to crash.
    crash_list_t crash_list;
    // Whether to bind to the thread exception port.
    bool bind_to_thread;
} test_data_t;

// This is run as a separate thread, so exit() is used instead of returning
// status values.
static int run_test(void* arg) {
    test_data_t* data = (test_data_t*)arg;
    mx_handle_t self = mx_thread_self();

    // We need to register for thread termination here instead of the main
    // thread. The main thread can't get a handle to this thread before it has
    // started, at which point the test may have run and crashed already,
    // leading to an invalid handle.
    mx_status_t status = mx_object_wait_async(self, data->port,
                                              TEST_THREAD_TERMINATED_KEY,
                                              MX_THREAD_TERMINATED,
                                              MX_WAIT_ASYNC_ONCE);
    if (status != MX_OK) {
        UNITTEST_TRACEF(
            "FATAL: failed to wait on test thread termination : error %s\n",
            mx_status_get_string(status));
        exit(MX_ERR_INTERNAL);
    }

    // We also can't do this in the main thread as we wouldn't have the
    // thread handle yet.
    if (data->bind_to_thread) {
        status = mx_task_bind_exception_port(self, data->port,
                                             EXCEPTION_PORT_KEY, 0);
        if (status != MX_OK) {
            UNITTEST_TRACEF("FATAL: failed to bind to exception port: error %s\n",
                            mx_status_get_string(status));
            exit(MX_ERR_INTERNAL);
        }
        crash_list_register(data->crash_list, self);
    }

    bool test_result = data->test_function(data->test_function_arg);

    // Notify the crash handler of the test result before returning.
    // We can't just return the test result as the test thread could
    // be registered to crash, so the crash handler can't use thrd_join.
    uint32_t signal = test_result ? TEST_PASSED_SIGNAL : TEST_FAILED_SIGNAL;
    status = mx_object_signal(data->test_ended_event, 0, signal);
    if (status != MX_OK) {
        UNITTEST_TRACEF("FATAL: failed to signal test result : error %s\n",
                        mx_status_get_string(status));
        exit(MX_ERR_INTERNAL);
    }
    return 0;
}

// Runs the function in a separate thread with the given argument,
// catching any crashes.
// If bind_to_job is true, this will bind to the job exception port
// before starting the test.
// If false, this will bind to the test thread's exception port once started
// and add the thread to the expected crashes list.
mx_status_t run_with_crash_handler(crash_list_t crash_list,
                                   bool (*fn_to_run)(void*), void* arg,
                                   bool bind_to_job,
                                   test_result_t* test_result) {
    mx_handle_t port;
    mx_status_t status = mx_port_create(0, &port);
    if (status != MX_OK) {
        UNITTEST_TRACEF("failed to create port: error %s\n",
                        mx_status_get_string(status));
        return status;
    }
    if (bind_to_job) {
        status = mx_task_bind_exception_port(mx_job_default(), port,
                                             EXCEPTION_PORT_KEY, 0);
        if (status != MX_OK) {
            UNITTEST_TRACEF("failed to bind to exception port: error %s\n",
                            mx_status_get_string(status));
            mx_handle_close(port);
            return status;
        }
    }

    mx_handle_t test_ended_event;
    status = mx_event_create(0, &test_ended_event);
    if (status != MX_OK) {
        UNITTEST_TRACEF("failed to create event: error %s\n",
                        mx_status_get_string(status));
        mx_handle_close(port);
        return status;
    }
    status = mx_object_wait_async(test_ended_event, port, TEST_ENDED_EVENT_KEY,
                                  TEST_PASSED_SIGNAL | TEST_FAILED_SIGNAL,
                                  MX_WAIT_ASYNC_ONCE);
    if (status != MX_OK) {
        UNITTEST_TRACEF(
            "failed to wait on test_ended_event: error %s\n",
            mx_status_get_string(status));
        mx_handle_close(port);
        mx_handle_close(test_ended_event);
        return status;
    }

    // Run the test in a separate thread in case it crashes.
    thrd_t test_thread;
    test_data_t test_data = {
        .test_function = fn_to_run,
        .test_function_arg = arg,
        .test_ended_event = test_ended_event,
        .port = port,
        .crash_list = crash_list,
        .bind_to_thread = !bind_to_job};

    int thrd_res = thrd_create(&test_thread, run_test, (void*)&test_data);
    if (thrd_res != thrd_success) {
        UNITTEST_TRACEF("failed to create test thread\n");
        mx_handle_close(port);
        mx_handle_close(test_ended_event);
        return thrd_status_to_mx_status(thrd_res);
    }

    // The test thread will signal on the test_ended event when it completes,
    // or the crash handler will catch it crashing.
    *test_result = watch_test_thread(port, crash_list);

    mx_handle_close(port);
    mx_handle_close(test_ended_event);

    return MX_OK;
}

typedef struct {
    bool (*fn)(void);
} test_wrapper_arg_t;

static bool test_wrapper(void* arg) {
    return ((test_wrapper_arg_t*)arg)->fn();
}

mx_status_t run_test_with_crash_handler(crash_list_t crash_list,
                                        bool (*test_to_run)(void),
                                        test_result_t* test_result) {
    test_wrapper_arg_t twarg = {.fn = test_to_run};

    return run_with_crash_handler(crash_list, test_wrapper, &twarg,
                                  true, test_result);
}

typedef struct {
    void (*fn)(void*);
    void* arg;
} crash_fn_wrapper_arg_t;

static bool crash_fn_wrapper(void* arg) {
    crash_fn_wrapper_arg_t* cfwarg = (crash_fn_wrapper_arg_t*)arg;
    cfwarg->fn(cfwarg->arg);
    // The function is expected to crash and shouldn't get to here.
    return false;
}

mx_status_t run_fn_with_crash_handler(void (*fn_to_run)(void*), void* arg,
                                      test_result_t* test_result) {
    crash_list_t crash_list = crash_list_new();
    crash_fn_wrapper_arg_t cfwarg = {.fn = fn_to_run, .arg = arg};

    mx_status_t status = run_with_crash_handler(crash_list,
                                                crash_fn_wrapper, &cfwarg,
                                                false, test_result);

    crash_list_delete(crash_list);

    return status;
}
