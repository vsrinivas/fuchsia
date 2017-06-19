// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <lib/console.h>
#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>
#include <lib/ktrace.h>
#include <lib/mtrace.h>
#include <lib/io.h>

#include <platform/debug.h>

#include <magenta/handle_owner.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/debug.h>
#include <magenta/user_copy.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxDebugWriteSize = 256u;

mx_status_t sys_debug_read(mx_handle_t handle, void* ptr, uint32_t len) {
    LTRACEF("ptr %p\n", ptr);

    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    if (!len)
        return 0;
    // TODO: remove this cast.
    auto uptr = reinterpret_cast<uint8_t*>(ptr);
    auto end = uptr + len;

    for (; uptr != end; ++uptr) {
        int c = getchar();
        if (c < 0)
            break;

        if (c == '\r')
            c = '\n';
        if (copy_to_user_u8_unsafe(uptr, static_cast<uint8_t>(c)) != MX_OK)
            break;
    }
    // TODO: fix this cast, which can overflow.
    return static_cast<int>(reinterpret_cast<char*>(uptr) - reinterpret_cast<char*>(ptr));
}

mx_status_t sys_debug_write(const void* ptr, uint32_t len) {
    LTRACEF("ptr %p, len %u\n", ptr, len);

    if (len > kMaxDebugWriteSize)
        len = kMaxDebugWriteSize;

    char buf[kMaxDebugWriteSize];
    if (magenta_copy_from_user(ptr, buf, len) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    __kernel_serial_write(buf, len);

    return len;
}

mx_status_t sys_debug_send_command(mx_handle_t handle, const void* ptr, uint32_t len) {
    LTRACEF("ptr %p, len %u\n", ptr, len);

    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    if (len > kMaxDebugWriteSize)
        return MX_ERR_INVALID_ARGS;

    char buf[kMaxDebugWriteSize + 2];
    if (magenta_copy_from_user(ptr, buf, len) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    buf[len] = '\n';
    buf[len + 1] = 0;
    return console_run_script(buf);
}

mx_handle_t sys_debug_transfer_handle(mx_handle_t proc, mx_handle_t src_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<ProcessDispatcher> process;
    mx_status_t status = up->GetDispatcherWithRights(proc, MX_RIGHT_READ | MX_RIGHT_WRITE,
                                                     &process);
    if (status != MX_OK)
        return status;

    // Disallow this call on self.
    if (process.get() == up)
        return MX_ERR_INVALID_ARGS;

    HandleOwner handle = up->RemoveHandle(src_handle);
    if (!handle)
        return MX_ERR_BAD_HANDLE;

    auto dest_hv = process->MapHandleToValue(handle);
    process->AddHandle(mxtl::move(handle));
    return dest_hv;
}

mx_status_t sys_ktrace_read(mx_handle_t handle, user_ptr<void> _data,
                            uint32_t offset, uint32_t len,
                            user_ptr<uint32_t> _actual) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    int result = ktrace_read_user(_data.get(), offset, len);
    if (result < 0)
        return result;

    return _actual.copy_to_user(static_cast<uint32_t>(result));
}

mx_status_t sys_ktrace_control(
        mx_handle_t handle, uint32_t action,uint32_t options, user_ptr<void> _ptr) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    switch (action) {
    case KTRACE_ACTION_NEW_PROBE: {
        char name[MX_MAX_NAME_LEN];
        if (_ptr.copy_array_from_user(name, sizeof(name) - 1) != MX_OK)
            return MX_ERR_INVALID_ARGS;
        name[sizeof(name) - 1] = 0;
        return ktrace_control(action, options, name);
    }
    default:
        return ktrace_control(action, options, nullptr);
    }
}

mx_status_t sys_ktrace_write(mx_handle_t handle, uint32_t event_id, uint32_t arg0, uint32_t arg1) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    if (event_id > 0x7FF) {
        return MX_ERR_INVALID_ARGS;
    }

    uint32_t* args = static_cast<uint32_t*>(ktrace_open(TAG_PROBE_24(event_id)));
    if (!args) {
        //  There is not a single reason for failure. Assume it reached the end.
        return MX_ERR_UNAVAILABLE;
    }

    args[0] = arg0;
    args[1] = arg1;
    return MX_OK;
}

mx_status_t sys_mtrace_control(mx_handle_t handle,
                               uint32_t kind, uint32_t action, uint32_t options,
                               void* _ptr, uint32_t size) {
    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    return mtrace_control(kind, action, options, _ptr, size);
}
