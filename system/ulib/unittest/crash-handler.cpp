// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-handler.h"

#include <inttypes.h>

#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#define EXCEPTION_PORT_KEY 1
// The test completed without the test thread crashing.
#define TEST_ENDED_EVENT_KEY 2
// The test thread had a registered crash.
#define TEST_THREAD_TERMINATED_KEY 3

// Signals sent from the test thread to the crash handler port to indicate
// the test result.
#define TEST_PASSED_SIGNAL ZX_USER_SIGNAL_0
#define TEST_FAILED_SIGNAL ZX_USER_SIGNAL_1

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
static void process_exception(crash_list_t crash_list, const zx_port_packet_t* packet) {
    const zx_packet_exception_t* exception = &packet->exception;

    // Check for exceptions from registered processes that are not really crashes.
    switch (packet->type) {
    case ZX_EXCP_THREAD_STARTING:
    case ZX_EXCP_THREAD_EXITING: {
        zx_handle_t process = crash_list_lookup_koid(crash_list, exception->pid);
        zx_handle_t thread = ZX_HANDLE_INVALID;
        if (process == ZX_HANDLE_INVALID) {
            // The test may have registered a thread handle instead.
            thread = crash_list_lookup_koid(crash_list, exception->tid);
        }
        if (process != ZX_HANDLE_INVALID || thread != ZX_HANDLE_INVALID) {
            zx_status_t status;
            if (thread == ZX_HANDLE_INVALID) {
                status =
                    zx_object_get_child(process, exception->tid, ZX_RIGHT_SAME_RIGHTS, &thread);
                if (status != ZX_OK) {
                    UNITTEST_FAIL_TRACEF(
                        "FATAL: failed to get a handle to [%" PRIu64 "%." PRIu64 "] : error %s\n",
                        exception->pid, exception->tid, zx_status_get_string(status));
                    exit(ZX_ERR_INTERNAL);
                }
            }
            status = zx_task_resume(thread, ZX_RESUME_EXCEPTION);
            if (status != ZX_OK) {
                UNITTEST_FAIL_TRACEF("FATAL: failed to resume [%" PRIu64 ".%" PRIu64
                                     "] : error %s\n",
                                     exception->pid, exception->tid, zx_status_get_string(status));
                exit(ZX_ERR_INTERNAL);
            }
            return;
        }
        break;
    }
    default:
        break;
    }

    // Check if the crashed process is in the registered list and remove
    // it if so.
    zx_handle_t match = crash_list_delete_koid(crash_list, exception->pid);
    if (match == ZX_HANDLE_INVALID) {
        // The test may have registered a thread handle instead.
        match = crash_list_delete_koid(crash_list, exception->tid);
    }

    // The crash was not registered. We should let crashlogger print out the
    // details and then fail the test.
    if (match == ZX_HANDLE_INVALID) {
        UNITTEST_FAIL_TRACEF("FATAL: [%" PRIu64 ".%" PRIu64
                             "] crashed with exception 0x%x but was not registered\n",
                             exception->pid, exception->tid, packet->type);
        zx_handle_t process;
        zx_status_t status =
            zx_object_get_child(ZX_HANDLE_INVALID, exception->pid, ZX_RIGHT_SAME_RIGHTS, &process);
        if (status != ZX_OK) {
            UNITTEST_FAIL_TRACEF("FATAL: failed to get a handle to [%" PRIu64 "] : error %s\n",
                                 exception->pid, zx_status_get_string(status));
            exit(ZX_ERR_INTERNAL);
        }
        zx_handle_t thread;
        status = zx_object_get_child(process, exception->tid, ZX_RIGHT_SAME_RIGHTS, &thread);
        if (status != ZX_OK) {
            UNITTEST_FAIL_TRACEF("FATAL: failed to get a handle to [%" PRIu64 ".%" PRIu64
                                 "] : error %s\n",
                                 exception->pid, exception->tid, zx_status_get_string(status));
            zx_handle_close(process);
            exit(ZX_ERR_INTERNAL);
        }
        // Pass the exception up to crashlogger.
        status = zx_task_resume(thread, ZX_RESUME_EXCEPTION | ZX_RESUME_TRY_NEXT);
        if (status == ZX_OK) {
            // Give crashlogger a little time to print info about the crashed
            // thread.
            zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
        } else {
            UNITTEST_FAIL_TRACEF("FATAL: could not pass exception from [%" PRIu64 ".%" PRIu64
                                 "] : error %s\n",
                                 exception->pid, exception->tid, zx_status_get_string(status));
        }
        // This may not be reached if the test process itself crashed,
        // as crashlogger will kill the crashed process.
        zx_handle_close(process);
        zx_handle_close(thread);
        // TODO: fail the test more gracefully.
        exit(ZX_ERR_INTERNAL);
    }
    zx_status_t status = zx_task_kill(match);
    if (status != ZX_OK) {
        UNITTEST_FAIL_TRACEF("FATAL: failed to kill [%" PRIu64 ".%" PRIu64 "]  : error %s\n",
                             exception->pid, exception->tid, zx_status_get_string(status));
        exit(ZX_ERR_INTERNAL);
    }

    // The exception is still unprocessed. We should wait for termination so
    // there is no race condition with when we unbind the exception port.
    status = zx_object_wait_one(match, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, NULL);
    if (status != ZX_OK) {
        UNITTEST_FAIL_TRACEF("FATAL: failed to wait for termination  : error %s\n",
                             zx_status_get_string(status));
        exit(ZX_ERR_INTERNAL);
    }
    zx_handle_close(match);
}

// Returns the test result if it completes, else true if the test thread
// had a registered crash.
static test_result_t watch_test_thread(zx_handle_t port, crash_list_t crash_list) {
    zx_port_packet_t packet;
    while (true) {
        zx_status_t status = zx_port_wait(port, ZX_TIME_INFINITE, &packet, 1);
        if (status != ZX_OK) {
            UNITTEST_FAIL_TRACEF("failed to wait on port: error %s\n",
                                 zx_status_get_string(status));
            exit(ZX_ERR_INTERNAL);
        }
        switch (packet.key) {
        case EXCEPTION_PORT_KEY:
            process_exception(crash_list, &packet);
            break;
        case TEST_ENDED_EVENT_KEY:
            if (packet.signal.observed & TEST_PASSED_SIGNAL) {
                return TEST_PASSED;
            } else if (packet.signal.observed & TEST_FAILED_SIGNAL) {
                return TEST_FAILED;
            } else {
                UNITTEST_FAIL_TRACEF("unknown test ended event signal: %u\n",
                                     packet.signal.observed);
                exit(ZX_ERR_INTERNAL);
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

struct test_data_t {
    // The test function to call.
    bool (*test_function)(void*);
    void* test_function_arg;

    // For signaling TEST_PASSED_SIGNAL or TEST_FAILED_SIGNAL.
    zx_handle_t test_ended_event;
    // For registering test termination.
    zx_handle_t port;

    // For registering the test thread, if it is expected to crash.
    crash_list_t crash_list;
    // Whether to bind to the thread exception port.
    bool bind_to_thread;
};

// This is run as a separate thread, so exit() is used instead of returning
// status values.
static int run_test(void* arg) {
    test_data_t* data = (test_data_t*)arg;
    zx_handle_t self = zx_thread_self();

    // We need to register for thread termination here instead of the main
    // thread. The main thread can't get a handle to this thread before it has
    // started, at which point the test may have run and crashed already,
    // leading to an invalid handle.
    zx_status_t status = zx_object_wait_async(self, data->port, TEST_THREAD_TERMINATED_KEY,
                                              ZX_THREAD_TERMINATED, ZX_WAIT_ASYNC_ONCE);
    if (status != ZX_OK) {
        UNITTEST_FAIL_TRACEF("FATAL: failed to wait on test thread termination : error %s\n",
                             zx_status_get_string(status));
        exit(ZX_ERR_INTERNAL);
    }

    // We also can't do this in the main thread as we wouldn't have the
    // thread handle yet.
    if (data->bind_to_thread) {
        status = zx_task_bind_exception_port(self, data->port, EXCEPTION_PORT_KEY, 0);
        if (status != ZX_OK) {
            UNITTEST_FAIL_TRACEF("FATAL: failed to bind to exception port: error %s\n",
                                 zx_status_get_string(status));
            exit(ZX_ERR_INTERNAL);
        }
        crash_list_register(data->crash_list, self);
    }

    bool test_result = data->test_function(data->test_function_arg);

    // Notify the crash handler of the test result before returning.
    // We can't just return the test result as the test thread could
    // be registered to crash, so the crash handler can't use thrd_join.
    uint32_t signal = test_result ? TEST_PASSED_SIGNAL : TEST_FAILED_SIGNAL;
    status = zx_object_signal(data->test_ended_event, 0, signal);
    if (status != ZX_OK) {
        UNITTEST_FAIL_TRACEF("FATAL: failed to signal test result : error %s\n",
                             zx_status_get_string(status));
        exit(ZX_ERR_INTERNAL);
    }
    return 0;
}

// Runs the function in a separate thread with the given argument,
// catching any crashes.
// If bind_to_job is true, this will bind to the job exception port
// before starting the test.
// If false, this will bind to the test thread's exception port once started
// and add the thread to the expected crashes list.
zx_status_t run_with_crash_handler(crash_list_t crash_list, bool (*fn_to_run)(void*), void* arg,
                                   bool bind_to_job, test_result_t* test_result) {
    zx_handle_t port;
    zx_status_t status = zx_port_create(0, &port);
    if (status != ZX_OK) {
        UNITTEST_FAIL_TRACEF("failed to create port: error %s\n", zx_status_get_string(status));
        return status;
    }
    if (bind_to_job) {
        status = zx_task_bind_exception_port(zx_job_default(), port, EXCEPTION_PORT_KEY, 0);
        if (status != ZX_OK) {
            UNITTEST_FAIL_TRACEF("failed to bind to exception port: error %s\n",
                                 zx_status_get_string(status));
            zx_handle_close(port);
            return status;
        }
    }

    zx_handle_t test_ended_event;
    status = zx_event_create(0, &test_ended_event);
    if (status != ZX_OK) {
        UNITTEST_FAIL_TRACEF("failed to create event: error %s\n", zx_status_get_string(status));
        zx_handle_close(port);
        return status;
    }
    status = zx_object_wait_async(test_ended_event, port, TEST_ENDED_EVENT_KEY,
                                  TEST_PASSED_SIGNAL | TEST_FAILED_SIGNAL, ZX_WAIT_ASYNC_ONCE);
    if (status != ZX_OK) {
        UNITTEST_FAIL_TRACEF("failed to wait on test_ended_event: error %s\n",
                             zx_status_get_string(status));
        zx_handle_close(port);
        zx_handle_close(test_ended_event);
        return status;
    }

    // Run the test in a separate thread in case it crashes.
    thrd_t test_thread;
    test_data_t test_data = {.test_function = fn_to_run,
                             .test_function_arg = arg,
                             .test_ended_event = test_ended_event,
                             .port = port,
                             .crash_list = crash_list,
                             .bind_to_thread = !bind_to_job};

    int thrd_res = thrd_create(&test_thread, run_test, (void*)&test_data);
    if (thrd_res != thrd_success) {
        UNITTEST_FAIL_TRACEF("failed to create test thread\n");
        zx_handle_close(port);
        zx_handle_close(test_ended_event);
        return thrd_status_to_zx_status(thrd_res);
    }

    // The test thread will signal on the test_ended event when it completes,
    // or the crash handler will catch it crashing.
    *test_result = watch_test_thread(port, crash_list);

    zx_handle_close(port);
    zx_handle_close(test_ended_event);

    return ZX_OK;
}

struct test_wrapper_arg_t {
    bool (*fn)(void);
};

static bool test_wrapper(void* arg) {
    return static_cast<test_wrapper_arg_t*>(arg)->fn();
}

zx_status_t run_test_with_crash_handler(crash_list_t crash_list, bool (*test_to_run)(),
                                        test_result_t* test_result) {
    test_wrapper_arg_t twarg = {.fn = test_to_run};

    return run_with_crash_handler(crash_list, test_wrapper, &twarg, true, test_result);
}

struct crash_fn_wrapper_arg_t {
    void (*fn)(void*);
    void* arg;
};

static bool crash_fn_wrapper(void* arg) {
    crash_fn_wrapper_arg_t* cfwarg = static_cast<crash_fn_wrapper_arg_t*>(arg);
    cfwarg->fn(cfwarg->arg);
    // The function is expected to crash and shouldn't get to here.
    return false;
}

zx_status_t run_fn_with_crash_handler(void (*fn_to_run)(void*), void* arg,
                                      test_result_t* test_result) {
    crash_list_t crash_list = crash_list_new();
    crash_fn_wrapper_arg_t cfwarg = {.fn = fn_to_run, .arg = arg};

    zx_status_t status =
        run_with_crash_handler(crash_list, crash_fn_wrapper, &cfwarg, false, test_result);

    crash_list_delete(crash_list);

    return status;
}
