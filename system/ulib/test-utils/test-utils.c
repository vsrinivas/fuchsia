// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <zircon/crashlogger.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <zircon/status.h>
#include <runtime/thread.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#define TU_FAIL_ERRCODE 10
#define TU_WATCHDOG_ERRCODE 5

static int timeout_scale = 1;

static thrd_t watchdog_thread;

void* tu_malloc(size_t size)
{
    void* result = malloc(size);
    if (result == NULL) {
        // TODO(dje): printf may try to malloc too ...
        unittest_printf_critical("out of memory trying to malloc(%zu)\n", size);
        exit(TU_FAIL_ERRCODE);
    }
    return result;
}

void* tu_calloc(size_t nmemb, size_t size)
{
    void* result = calloc(nmemb, size);
    if (result == NULL) {
        // TODO(dje): printf may try to malloc too ...
        unittest_printf_critical("out of memory trying to calloc(%zu, %zu)\n", nmemb, size);
        exit(TU_FAIL_ERRCODE);
    }
    return result;
}

char* tu_strdup(const char* s)
{
    size_t len = strlen(s) + 1;
    char* r = tu_malloc(len);
    strcpy(r, s);
    return r;
}

char* tu_asprintf(const char* fmt, ...)
{
    va_list args;
    char* result;
    va_start(args, fmt);
    if (vasprintf(&result, fmt, args) < 0) {
        unittest_printf_critical("out of memory trying to asprintf(%s)\n", fmt);
        exit(TU_FAIL_ERRCODE);
    }
    va_end(args);
    return result;
}

void tu_fatal(const char *what, zx_status_t status)
{
    const char* reason = zx_status_get_string(status);
    unittest_printf_critical("\nFATAL: %s failed, rc %d (%s)\n", what, status, reason);

    // Request a backtrace to assist debugging.
    unittest_printf_critical("FATAL: backtrace follows:\n");
    unittest_printf_critical("       (using sw breakpoint request to crashlogger)\n");
    crashlogger_request_backtrace();

    unittest_printf_critical("FATAL: exiting process\n");
    exit(TU_FAIL_ERRCODE);
}

void tu_handle_close(zx_handle_t handle)
{
    zx_status_t status = zx_handle_close(handle);
    // TODO(dje): It's still an open question as to whether errors other than ZX_ERR_BAD_HANDLE are "advisory".
    if (status < 0) {
        tu_fatal(__func__, status);
    }
}

zx_handle_t tu_handle_duplicate(zx_handle_t handle)
{
    zx_handle_t copy = ZX_HANDLE_INVALID;
    zx_status_t status =
        zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &copy);
    if (status < 0)
        tu_fatal(__func__, status);
    return copy;
}

// N.B. This creates a C11 thread.
// See, e.g., musl/include/threads.h.

void tu_thread_create_c11(thrd_t* t, thrd_start_t entry, void* arg,
                          const char* name)
{
    int ret = thrd_create_with_name(t, entry, arg, name);
    if (ret != thrd_success) {
        // tu_fatal takes zx_status_t values.
        // The translation doesn't have to be perfect.
        switch (ret) {
        case thrd_nomem:
            tu_fatal(__func__, ZX_ERR_NO_MEMORY);
        default:
            tu_fatal(__func__, ZX_ERR_BAD_STATE);
        }
        __UNREACHABLE;
    }
}

zx_status_t tu_wait(uint32_t num_objects,
                    const zx_handle_t* handles,
                    const zx_signals_t* signals,
                    zx_signals_t* pending,
                    zx_time_t timeout)
{
    zx_wait_item_t items[num_objects];
    for (uint32_t n = 0; n < num_objects; n++) {
        items[n].handle = handles[n];
        items[n].waitfor = signals[n];
    }
    if (timeout != ZX_TIME_INFINITE) {
        zx_time_t scaled_timeout = timeout * timeout_scale;
        // Overflow -> infinity.
        if (scaled_timeout < timeout)
            timeout = ZX_TIME_INFINITE;
        else
            timeout = scaled_timeout;
    }
    zx_time_t deadline = zx_deadline_after(timeout);
    zx_status_t status = zx_object_wait_many(items, num_objects, deadline);
    for (uint32_t n = 0; n < num_objects; n++) {
        pending[n] = items[n].pending;
    }
    return status;
}

void tu_channel_create(zx_handle_t* handle0, zx_handle_t* handle1) {
    zx_handle_t handles[2];
    zx_status_t status = zx_channel_create(0, &handles[0], &handles[1]);
    if (status < 0)
        tu_fatal(__func__, status);
    *handle0 = handles[0];
    *handle1 = handles[1];
}

void tu_channel_write(zx_handle_t handle, uint32_t flags, const void* bytes, uint32_t num_bytes,
                      const zx_handle_t* handles, uint32_t num_handles) {
    zx_status_t status = zx_channel_write(handle, flags, bytes, num_bytes, handles, num_handles);
    if (status < 0)
        tu_fatal(__func__, status);
}

void tu_channel_read(zx_handle_t handle, uint32_t flags, void* bytes, uint32_t* num_bytes,
                     zx_handle_t* handles, uint32_t* num_handles) {
    zx_status_t status = zx_channel_read(handle, flags, bytes, handles,
                                         num_bytes ? *num_bytes : 0, num_handles ? *num_handles : 0,
                                         num_bytes, num_handles);
    if (status < 0)
        tu_fatal(__func__, status);
}

bool tu_channel_wait_readable(zx_handle_t channel)
{
    zx_signals_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    zx_signals_t pending;
    zx_status_t result = tu_wait(1, &channel, &signals, &pending, ZX_TIME_INFINITE);
    if (result != ZX_OK)
        tu_fatal(__func__, result);
    if ((pending & ZX_CHANNEL_READABLE) == 0) {
        unittest_printf("%s: peer closed\n", __func__);
        return false;
    }
    return true;
}

zx_handle_t tu_launch(zx_handle_t job, const char* name,
                      int argc, const char* const* argv,
                      const char* const* envp,
                      size_t num_handles, zx_handle_t* handles,
                      uint32_t* handle_ids)
{
    launchpad_t* lp;
    launchpad_create(job, name, &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);
    launchpad_add_handles(lp, num_handles, handles, handle_ids);

    zx_status_t status;
    zx_handle_t child;
    status = launchpad_go(lp, &child, NULL);

    if (status < 0)
        tu_fatal("tu_launch", status);
    return child;
}

launchpad_t* tu_launch_fdio_init(zx_handle_t job, const char* name,
                                 int argc, const char* const* argv,
                                 const char* const* envp,
                                 size_t hnds_count, zx_handle_t* handles,
                                 uint32_t* ids)
{
    // This is the first part of launchpad_launch_fdio_etc.
    // It does everything except start the process running.
    launchpad_t* lp;

    const char* filename = argv[0];
    if (name == NULL)
        name = filename;

    launchpad_create(job, name, &lp);
    launchpad_load_from_file(lp, filename);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);
    launchpad_clone(lp, LP_CLONE_FDIO_ALL);
    launchpad_add_handles(lp, hnds_count, handles, ids);

   return lp;
}

zx_handle_t tu_launch_fdio_fini(launchpad_t* lp)
{
    zx_handle_t proc;
    zx_status_t status;
    if ((status = launchpad_go(lp, &proc, NULL)) < 0)
        tu_fatal("tu_launch_fdio_fini", status);
    return proc;
}

void tu_process_wait_signaled(zx_handle_t process)
{
    zx_signals_t signals = ZX_PROCESS_TERMINATED;
    zx_signals_t pending;
    zx_status_t result = tu_wait(1, &process, &signals, &pending, ZX_TIME_INFINITE);
    if (result != ZX_OK)
        tu_fatal(__func__, result);
    if ((pending & ZX_PROCESS_TERMINATED) == 0) {
        unittest_printf_critical("%s: unexpected return from tu_wait\n", __func__);
        exit(TU_FAIL_ERRCODE);
    }
}

bool tu_process_has_exited(zx_handle_t process)
{
    zx_info_process_t info;
    zx_status_t status;
    if ((status = zx_object_get_info(process, ZX_INFO_PROCESS, &info,
                                     sizeof(info), NULL, NULL)) < 0)
        tu_fatal("get process info", status);
    return info.exited;
}

int tu_process_get_return_code(zx_handle_t process)
{
    zx_info_process_t info;
    zx_status_t status;
    if ((status = zx_object_get_info(process, ZX_INFO_PROCESS, &info,
                                     sizeof(info), NULL, NULL)) < 0)
        tu_fatal("get process info", status);
    if (!info.exited) {
        unittest_printf_critical(
                "attempt to read return code of non-exited process");
        exit(TU_FAIL_ERRCODE);
    }
    return info.return_code;
}

int tu_process_wait_exit(zx_handle_t process)
{
    tu_process_wait_signaled(process);
    return tu_process_get_return_code(process);
}

zx_handle_t tu_process_get_thread(zx_handle_t process, zx_koid_t tid)
{
    zx_handle_t thread;
    zx_status_t status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status == ZX_ERR_NOT_FOUND)
        return ZX_HANDLE_INVALID;
    if (status < 0)
        tu_fatal(__func__, status);
    return thread;
}

size_t tu_process_get_threads(zx_handle_t process, zx_koid_t* threads, size_t max_threads)
{
    size_t num_threads;
    size_t buf_size = max_threads * sizeof(threads[0]);
    zx_status_t status = zx_object_get_info(process, ZX_INFO_PROCESS_THREADS,
                                            threads, buf_size, &num_threads, NULL);
    if (status < 0)
        tu_fatal(__func__, status);
    return num_threads;
}

zx_handle_t tu_job_create(zx_handle_t job)
{
    zx_handle_t child_job;
    zx_status_t status = zx_job_create(job, 0, &child_job);
    if (status < 0)
        tu_fatal(__func__, status);
    return child_job;
}

zx_handle_t tu_io_port_create(void)
{
    zx_handle_t handle;
    zx_status_t status = zx_port_create(0, &handle);
    if (status < 0)
        tu_fatal(__func__, status);
    return handle;
}

void tu_set_exception_port(zx_handle_t handle, zx_handle_t eport, uint64_t key, uint32_t options)
{
    if (handle == 0)
        handle = zx_process_self();
    zx_status_t status = zx_task_bind_exception_port(handle, eport, key, options);
    if (status < 0)
        tu_fatal(__func__, status);
}

void tu_object_wait_async(zx_handle_t handle, zx_handle_t port, zx_signals_t signals)
{
    uint64_t key = tu_get_koid(handle);
    uint32_t options = ZX_WAIT_ASYNC_REPEATING;
    zx_status_t status = zx_object_wait_async(handle, port, key, signals, options);
    if (status < 0)
        tu_fatal(__func__, status);
}

void tu_handle_get_basic_info(zx_handle_t handle, zx_info_handle_basic_t* info)
{
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC,
                                            info, sizeof(*info), NULL, NULL);
    if (status < 0)
        tu_fatal(__func__, status);
}

zx_koid_t tu_get_koid(zx_handle_t handle)
{
    zx_info_handle_basic_t info;
    tu_handle_get_basic_info(handle, &info);
    return info.koid;
}

zx_koid_t tu_get_related_koid(zx_handle_t handle)
{
    zx_info_handle_basic_t info;
    tu_handle_get_basic_info(handle, &info);
    return info.related_koid;
}

zx_handle_t tu_get_thread(zx_handle_t proc, zx_koid_t tid)
{
    zx_handle_t thread;
    zx_status_t status = zx_object_get_child(proc, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status != ZX_OK)
        tu_fatal(__func__, status);
    return thread;
}

zx_info_thread_t tu_thread_get_info(zx_handle_t thread)
{
    zx_info_thread_t info;
    zx_status_t status = zx_object_get_info(thread, ZX_INFO_THREAD, &info, sizeof(info), NULL, NULL);
    if (status < 0)
        tu_fatal("zx_object_get_info(ZX_INFO_THREAD)", status);
    return info;
}

bool tu_thread_is_dying_or_dead(zx_handle_t thread)
{
    zx_info_thread_t info = tu_thread_get_info(thread);
    return (info.state == ZX_THREAD_STATE_DYING ||
            info.state == ZX_THREAD_STATE_DEAD);
}

int tu_run_program(const char *progname, int argc, const char** argv)
{
    launchpad_t* lp;

    unittest_printf("%s: running %s\n", __func__, progname);

    launchpad_create(ZX_HANDLE_INVALID, progname, &lp);
    launchpad_clone(lp, LP_CLONE_ALL);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    zx_status_t status;
    zx_handle_t child;
    if ((status = launchpad_go(lp, &child, NULL)) < 0) {
        tu_fatal(__func__, status);
    }

    int rc = tu_process_wait_exit(child);
    tu_handle_close(child);
    unittest_printf("%s: child returned %d\n", __func__, rc);
    return rc;
}

int tu_run_command(const char* progname, const char* cmd)
{
    const char* argv[] = {
        "/boot/bin/sh",
        "-c",
        cmd
    };

    return tu_run_program(progname, countof(argv), argv);
}

int tu_set_timeout_scale(int scale)
{
    int prev = timeout_scale;
    if (scale != 0)
        timeout_scale = scale;
    return prev;
}

// Setting to true when done turns off the watchdog timer. This
// must be an atomic so that the compiler does not assume anything
// about when it can be touched. Otherwise, since the compiler
// knows that vDSO calls don't make direct callbacks, it assumes
// that nothing can happen inside the watchdog loop that would touch
// this variable. In fact, it will be touched in parallel by another thread.
static volatile atomic_bool done_tests;

static int watchdog_thread_func(void* arg)
{
    for (int i = 0; i < TU_WATCHDOG_TIMEOUT_TICKS * timeout_scale; ++i) {
        zx_nanosleep(zx_deadline_after(TU_WATCHDOG_TICK_DURATION));
        if (atomic_load(&done_tests))
            return 0;
    }
    unittest_printf_critical("\n\n*** WATCHDOG TIMER FIRED ***\n");
    // This should *cleanly* kill the entire process, not just this thread.
    // TODO(dbort): Figure out why the shell sometimes reports a zero
    // exit status when we expect to see '5'.
    exit(TU_WATCHDOG_ERRCODE);
}

void tu_watchdog_start(void)
{
    atomic_store(&done_tests, false);
    tu_thread_create_c11(&watchdog_thread, watchdog_thread_func, NULL, "watchdog-thread");
}

void tu_watchdog_cancel(void)
{
    atomic_store(&done_tests, true);

    // TODO: Add an alarm as thrd_join doesn't provide a timeout.
    thrd_join(watchdog_thread, NULL);
}
