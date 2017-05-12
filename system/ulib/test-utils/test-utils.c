// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>
#include <magenta/status.h>
#include <runtime/thread.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#define TU_FAIL_ERRCODE 10

void* tu_malloc(size_t size)
{
    void* result = malloc(size);
    if (result == NULL) {
        // TODO(dje): printf may try to malloc too ...
        unittest_printf_critical("out of memory trying to allocate %zu bytes\n", size);
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

void tu_fatal(const char *what, mx_status_t status)
{
    const char* reason = mx_status_get_string(status);
    unittest_printf_critical("%s failed, rc %d (%s)\n", what, status, reason);
    exit(TU_FAIL_ERRCODE);
}

void tu_handle_close(mx_handle_t handle)
{
    mx_status_t status = mx_handle_close(handle);
    // TODO(dje): It's still an open question as to whether errors other than MX_ERR_BAD_HANDLE are "advisory".
    if (status < 0) {
        tu_fatal(__func__, status);
    }
}

// N.B. This creates a C11 thread.
// See, e.g., musl/include/threads.h.

void tu_thread_create_c11(thrd_t* t, thrd_start_t entry, void* arg,
                          const char* name)
{
    int ret = thrd_create_with_name(t, entry, arg, name);
    if (ret != thrd_success) {
        // tu_fatal takes mx_status_t values.
        // The translation doesn't have to be perfect.
        switch (ret) {
        case thrd_nomem:
            tu_fatal(__func__, MX_ERR_NO_MEMORY);
        default:
            tu_fatal(__func__, MX_ERR_BAD_STATE);
        }
        __UNREACHABLE;
    }
}

static mx_status_t tu_wait(const mx_handle_t* handles, const mx_signals_t* signals,
                           uint32_t num_handles, uint32_t* result_index,
                           mx_time_t timeout,
                           mx_signals_t* pending)
{
    mx_wait_item_t items[num_handles];
    for (uint32_t n = 0; n < num_handles; n++) {
        items[n].handle = handles[n];
        items[n].waitfor = signals[n];
    }
    mx_time_t deadline = mx_deadline_after(timeout);
    mx_status_t status = mx_object_wait_many(items, num_handles, deadline);
    for (uint32_t n = 0; n < num_handles; n++) {
        pending[n] = items[n].pending;
    }
    return status;
}

void tu_channel_create(mx_handle_t* handle0, mx_handle_t* handle1) {
    mx_handle_t handles[2];
    mx_status_t status = mx_channel_create(0, &handles[0], &handles[1]);
    if (status < 0)
        tu_fatal(__func__, status);
    *handle0 = handles[0];
    *handle1 = handles[1];
}

void tu_channel_write(mx_handle_t handle, uint32_t flags, const void* bytes, uint32_t num_bytes,
                      const mx_handle_t* handles, uint32_t num_handles) {
    mx_status_t status = mx_channel_write(handle, flags, bytes, num_bytes, handles, num_handles);
    if (status < 0)
        tu_fatal(__func__, status);
}

void tu_channel_read(mx_handle_t handle, uint32_t flags, void* bytes, uint32_t* num_bytes,
                     mx_handle_t* handles, uint32_t* num_handles) {
    mx_status_t status = mx_channel_read(handle, flags, bytes, handles,
                                         num_bytes ? *num_bytes : 0, num_handles ? *num_handles : 0,
                                         num_bytes, num_handles);
    if (status < 0)
        tu_fatal(__func__, status);
}

// Wait until |channel| is readable or peer is closed.
// Result is true if readable, otherwise false.

bool tu_channel_wait_readable(mx_handle_t channel)
{
    mx_signals_t signals = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    mx_signals_t pending;
    int64_t timeout = TU_WATCHDOG_DURATION_NANOSECONDS;
    mx_status_t result = tu_wait(&channel, &signals, 1, NULL, timeout, &pending);
    if (result != MX_OK)
        tu_fatal(__func__, result);
    if ((pending & MX_CHANNEL_READABLE) == 0) {
        unittest_printf("%s: peer closed\n", __func__);
        return false;
    }
    return true;
}

mx_handle_t tu_launch(mx_handle_t job, const char* name,
                      int argc, const char* const* argv,
                      const char* const* envp,
                      size_t num_handles, mx_handle_t* handles,
                      uint32_t* handle_ids)
{
    launchpad_t* lp;
    launchpad_create(job, name, &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);
    launchpad_add_handles(lp, num_handles, handles, handle_ids);

    mx_status_t status;
    mx_handle_t child;
    status = launchpad_go(lp, &child, NULL);

    if (status < 0)
        tu_fatal("tu_launch", status);
    return child;
}

launchpad_t* tu_launch_mxio_init(mx_handle_t job, const char* name,
                                 int argc, const char* const* argv,
                                 const char* const* envp,
                                 size_t hnds_count, mx_handle_t* handles,
                                 uint32_t* ids)
{
    // This is the first part of launchpad_launch_mxio_etc.
    // It does everything except start the process running.
    launchpad_t* lp;

    const char* filename = argv[0];
    if (name == NULL)
        name = filename;

    launchpad_create(job, name, &lp);
    launchpad_load_from_file(lp, filename);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);
    launchpad_clone(lp, LP_CLONE_MXIO_ALL);
    launchpad_add_handles(lp, hnds_count, handles, ids);

   return lp;
}

mx_handle_t tu_launch_mxio_fini(launchpad_t* lp)
{
    mx_handle_t proc;
    mx_status_t status;
    if ((status = launchpad_go(lp, &proc, NULL)) < 0)
        tu_fatal("tu_launch_mxio_fini", status);
    return proc;
}

void tu_process_wait_signaled(mx_handle_t process)
{
    mx_signals_t signals = MX_PROCESS_TERMINATED;
    mx_signals_t pending;
    int64_t timeout = TU_WATCHDOG_DURATION_NANOSECONDS;
    mx_status_t result = tu_wait(&process, &signals, 1, NULL, timeout, &pending);
    if (result != MX_OK)
        tu_fatal(__func__, result);
    if ((pending & MX_PROCESS_TERMINATED) == 0) {
        unittest_printf_critical("%s: unexpected return from tu_wait\n", __func__);
        exit(TU_FAIL_ERRCODE);
    }
}

bool tu_process_has_exited(mx_handle_t process)
{
    mx_info_process_t info;
    mx_status_t status;
    if ((status = mx_object_get_info(process, MX_INFO_PROCESS, &info,
                                     sizeof(info), NULL, NULL)) < 0)
        tu_fatal("get process info", status);
    return info.exited;
}

int tu_process_get_return_code(mx_handle_t process)
{
    mx_info_process_t info;
    mx_status_t status;
    if ((status = mx_object_get_info(process, MX_INFO_PROCESS, &info,
                                     sizeof(info), NULL, NULL)) < 0)
        tu_fatal("get process info", status);
    if (!info.exited) {
        unittest_printf_critical(
                "attempt to read return code of non-exited process");
        exit(TU_FAIL_ERRCODE);
    }
    return info.return_code;
}

int tu_process_wait_exit(mx_handle_t process)
{
    tu_process_wait_signaled(process);
    return tu_process_get_return_code(process);
}

mx_handle_t tu_job_create(mx_handle_t job)
{
    mx_handle_t child_job;
    mx_status_t status = mx_job_create(job, 0, &child_job);
    if (status < 0)
        tu_fatal(__func__, status);
    return child_job;
}

mx_handle_t tu_io_port_create(void)
{
    mx_handle_t handle;
    mx_status_t status = mx_port_create(0, &handle);
    if (status < 0)
        tu_fatal(__func__, status);
    return handle;
}

void tu_set_system_exception_port(mx_handle_t eport, uint64_t key)
{
    mx_status_t status = mx_task_bind_exception_port(0, eport, key, 0);
    if (status < 0)
        tu_fatal(__func__, status);
}

void tu_set_exception_port(mx_handle_t handle, mx_handle_t eport, uint64_t key, uint32_t options)
{
    if (handle == 0)
        handle = mx_process_self();
    mx_status_t status = mx_task_bind_exception_port(handle, eport, key, options);
    if (status < 0)
        tu_fatal(__func__, status);
}

void tu_handle_get_basic_info(mx_handle_t handle, mx_info_handle_basic_t* info)
{
    mx_status_t status = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC,
                                            info, sizeof(*info), NULL, NULL);
    if (status < 0)
        tu_fatal(__func__, status);
}

mx_koid_t tu_get_koid(mx_handle_t handle)
{
    mx_info_handle_basic_t info;
    tu_handle_get_basic_info(handle, &info);
    return info.koid;
}

mx_koid_t tu_get_related_koid(mx_handle_t handle)
{
    mx_info_handle_basic_t info;
    tu_handle_get_basic_info(handle, &info);
    return info.related_koid;
}

mx_handle_t tu_get_thread(mx_handle_t proc, mx_koid_t tid)
{
    mx_handle_t thread;
    mx_status_t status = mx_object_get_child(proc, tid, MX_RIGHT_SAME_RIGHTS, &thread);
    if (status != MX_OK)
        tu_fatal(__func__, status);
    return thread;
}

mx_info_thread_t tu_thread_get_info(mx_handle_t thread)
{
    mx_info_thread_t info;
    mx_status_t status = mx_object_get_info(thread, MX_INFO_THREAD, &info, sizeof(info), NULL, NULL);
    if (status < 0)
        tu_fatal("mx_object_get_info(MX_INFO_THREAD)", status);
    return info;
}

int tu_run_program(const char *progname, int argc, const char** argv)
{
    launchpad_t* lp;

    unittest_printf("%s: running %s\n", __func__, progname);

    launchpad_create(MX_HANDLE_INVALID, progname, &lp);
    launchpad_clone(lp, LP_CLONE_ALL);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    mx_status_t status;
    mx_handle_t child;
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
