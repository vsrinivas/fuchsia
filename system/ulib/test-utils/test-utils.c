// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/syscalls.h>
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
    // TODO(dje): It's still an open question as to whether errors other than ERR_BAD_HANDLE are "advisory".
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
            tu_fatal(__func__, ERR_NO_MEMORY);
        default:
            tu_fatal(__func__, ERR_BAD_STATE);
        }
        __UNREACHABLE;
    }
}

static mx_status_t tu_wait(const mx_handle_t* handles, const mx_signals_t* signals,
                           uint32_t num_handles, uint32_t* result_index,
                           mx_time_t deadline,
                           mx_signals_t* pending)
{
    mx_wait_item_t items[num_handles];
    for (uint32_t n = 0; n < num_handles; n++) {
        items[n].handle = handles[n];
        items[n].waitfor = signals[n];
    }
    mx_status_t status = mx_handle_wait_many(items, num_handles, deadline);
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
    mx_status_t status = mx_channel_read(handle, flags,
                                         bytes, num_bytes ? *num_bytes : 0, num_bytes,
                                         handles, num_handles ? *num_handles : 0, num_handles);
    if (status < 0)
        tu_fatal(__func__, status);
}

// Wait until |handle| is readable or peer is closed.
// Result is true if readable, otherwise false.

bool tu_wait_readable(mx_handle_t handle)
{
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    mx_signals_t pending;
    int64_t timeout = TU_WATCHDOG_DURATION_NANOSECONDS;
    mx_status_t result = tu_wait(&handle, &signals, 1, NULL, timeout, &pending);
    if (result != NO_ERROR)
        tu_fatal(__func__, result);
    if ((pending & MX_SIGNAL_READABLE) == 0) {
        unittest_printf("%s: peer closed\n", __func__);
        return false;
    }
    return true;
}

void tu_wait_signaled(mx_handle_t handle)
{
    mx_signals_t signals = MX_SIGNAL_SIGNALED;
    mx_signals_t pending;
    int64_t timeout = TU_WATCHDOG_DURATION_NANOSECONDS;
    mx_status_t result = tu_wait(&handle, &signals, 1, NULL, timeout, &pending);
    if (result != NO_ERROR)
        tu_fatal(__func__, result);
    if ((pending & MX_SIGNAL_SIGNALED) == 0) {
        unittest_printf_critical("%s: unexpected return from tu_wait\n", __func__);
        exit(TU_FAIL_ERRCODE);
    }
}

mx_handle_t tu_launch(const char* name,
                      int argc, const char* const* argv,
                      const char* const* envp,
                      size_t num_handles, mx_handle_t* handles,
                      uint32_t* handle_ids)
{
    mx_handle_t child = launchpad_launch(name, argc, argv, envp,
                                         num_handles, handles, handle_ids);
    if (child < 0)
        tu_fatal("launchpad_launch", child);
    return child;
}

launchpad_t* tu_launch_mxio_init(const char* name,
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

    mx_status_t status = launchpad_create(0u, name, &lp);
    if (status == NO_ERROR) {
        status = launchpad_elf_load(lp, launchpad_vmo_from_file(filename));
        if (status == NO_ERROR)
            status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
        if (status == NO_ERROR)
            status = launchpad_add_vdso_vmo(lp);
        if (status == NO_ERROR)
            status = launchpad_arguments(lp, argc, argv);
        if (status == NO_ERROR)
            status = launchpad_environ(lp, envp);
        if (status == NO_ERROR)
            status = launchpad_add_all_mxio(lp);
        if (status == NO_ERROR)
            status = launchpad_add_handles(lp, hnds_count, handles, ids);
    }

    if (status != NO_ERROR)
        tu_fatal("tu_launchpad_mxio_init", status);
    return lp;
}

mx_handle_t tu_launch_mxio_fini(launchpad_t* lp)
{
    // This is just launchpad's finish_launch function, but it's not exported.
    mx_handle_t proc;
    proc = launchpad_start(lp);
    launchpad_destroy(lp);
    if (proc < 0)
        tu_fatal("tu_launch_mxio_fini", proc);
    return proc;
}

int tu_process_get_return_code(mx_handle_t process)
{
    mx_info_process_t info;
    mx_status_t status;
    if ((status = mx_object_get_info(process, MX_INFO_PROCESS, &info,
                                     sizeof(info), NULL, NULL)) < 0)
        tu_fatal("get process info", status);
    return info.return_code;
}

int tu_process_wait_exit(mx_handle_t process)
{
    tu_wait_signaled(process);
    return tu_process_get_return_code(process);
}

mx_handle_t tu_io_port_create(uint32_t options)
{
    mx_handle_t handle;
    mx_status_t status = mx_port_create(options, &handle);
    if (status < 0)
        tu_fatal(__func__, status);
    return handle;
}

void tu_set_system_exception_port(mx_handle_t eport, uint64_t key)
{
    mx_status_t status = mx_object_bind_exception_port(0, eport, key, 0);
    if (status < 0)
        tu_fatal(__func__, status);
}

void tu_set_exception_port(mx_handle_t handle, mx_handle_t eport, uint64_t key, uint32_t options)
{
    if (handle == 0)
        handle = mx_process_self();
    mx_status_t status = mx_object_bind_exception_port(handle, eport, key, options);
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
