// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <launchpad/launchpad.h>
#include <magenta/syscalls.h>
#include <runtime/status.h>
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

void tu_fatal(const char *what, mx_status_t status)
{
    const char* reason = mx_strstatus(status);
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
                           mx_signals_state_t* signals_states)
{
    mx_status_t result;

    if (num_handles == 1u) {
        result =
            mx_handle_wait_one(*handles, *signals, deadline, signals_states);
    } else {
        result = mx_handle_wait_many(num_handles, handles, signals, deadline, NULL,
                                     signals_states);
    }

    // xyzdje, from mx_wait: TODO(cpu): implement |result_index|, see MG-33 bug.
    return result;
}

void tu_message_pipe_create(mx_handle_t* handle0, mx_handle_t* handle1)
{
    mx_handle_t handles[2];
    mx_status_t status = mx_msgpipe_create(handles, 0);
    if (status < 0)
        tu_fatal(__func__, status);
    *handle0 = handles[0];
    *handle1 = handles[1];
}

void tu_message_write(mx_handle_t handle, const void* bytes, uint32_t num_bytes,
                      const mx_handle_t* handles, uint32_t num_handles, uint32_t flags)
{
    mx_status_t status = mx_msgpipe_write(handle, bytes, num_bytes, handles, num_handles, flags);
    if (status < 0)
        tu_fatal(__func__, status);
}

void tu_message_read(mx_handle_t handle, void* bytes, uint32_t* num_bytes,
                     mx_handle_t* handles, uint32_t* num_handles, uint32_t flags)
{
    mx_status_t status = mx_msgpipe_read(handle, bytes, num_bytes, handles, num_handles, flags);
    if (status < 0)
        tu_fatal(__func__, status);
}

// Wait until |handle| is readable or peer is closed.
// Result is true if readable, otherwise false.

bool tu_wait_readable(mx_handle_t handle)
{
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    mx_signals_state_t signals_state;
    int64_t timeout = TU_WATCHDOG_DURATION_NANOSECONDS;
    mx_status_t result = tu_wait(&handle, &signals, 1, NULL, timeout, &signals_state);
    if (result != NO_ERROR)
        tu_fatal(__func__, result);
    if ((signals_state.satisfied & MX_SIGNAL_READABLE) == 0) {
        unittest_printf("%s: peer closed\n", __func__);
        return false;
    }
    return true;
}

void tu_wait_signaled(mx_handle_t handle)
{
    mx_signals_t signals = MX_SIGNAL_SIGNALED;
    mx_signals_state_t signals_state;
    int64_t timeout = TU_WATCHDOG_DURATION_NANOSECONDS;
    mx_status_t result = tu_wait(&handle, &signals, 1, NULL, timeout, &signals_state);
    if (result != NO_ERROR)
        tu_fatal(__func__, result);
    if ((signals_state.satisfied & MX_SIGNAL_SIGNALED) == 0) {
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

mx_handle_t tu_launch_mxio_etc(const char* name,
                               int argc, const char* const* argv,
                               const char* const* envp,
                               size_t num_handles, mx_handle_t* handles,
                               uint32_t* handle_ids)
{
    mx_handle_t child = launchpad_launch_mxio_etc(name, argc, argv, envp,
                                                  num_handles, handles, handle_ids);
    if (child < 0)
        tu_fatal("launchpad_launch_mxio_etc", child);
    return child;
}

int tu_process_get_return_code(mx_handle_t process)
{
    mx_info_process_t info;
    mx_ssize_t ret = mx_object_get_info(process, MX_INFO_PROCESS, sizeof(info.rec), &info, sizeof(info));
    if (ret < 0)
        tu_fatal("get process info", ret);
    if (ret != sizeof(info)) {
        // Bleah. Kernel/App mismatch?
        unittest_printf_critical("%s: unexpected result from mx_object_get_info\n", __func__);
        exit(TU_FAIL_ERRCODE);
    }
    return info.rec.return_code;
}

int tu_process_wait_exit(mx_handle_t process)
{
    tu_wait_signaled(process);
    return tu_process_get_return_code(process);
}

mx_handle_t tu_io_port_create(uint32_t options)
{
    mx_handle_t handle = mx_port_create(options);
    if (handle < 0)
        tu_fatal(__func__, handle);
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
    mx_status_t status = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, sizeof(info->rec),
                                            info, sizeof(*info));
    if (status < 0)
        tu_fatal(__func__, status);
}
