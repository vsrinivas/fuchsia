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
#include <lib/debuglog.h>
#include <lib/user_copy/user_ptr.h>
#include <lib/ktrace.h>
#include <lib/mtrace.h>
#include <lib/io.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/resource.h>

#include <platform/debug.h>

#include <zircon/syscalls/debug.h>

#include "priv.h"

#define LOCAL_TRACE 0

constexpr uint32_t kMaxDebugWriteSize = 256u;

zx_status_t sys_debug_read(zx_handle_t handle, user_out_ptr<char> ptr, user_inout_ptr<size_t> len) {
    LTRACEF("ptr %p\n", ptr.get());

    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    // get the number of bytes the user wants us to read
    size_t readlen;
    status = len.copy_from_user(&readlen);
    if (status != ZX_OK) {
        return status;
    }

    size_t idx = 0;
    for (; idx < readlen; ++idx) {
        int intc = getchar();
        if (intc < 0) {
            return intc;
        }

        if (intc == '\r') {
            intc = '\n';
        }

        char c = static_cast<char>(intc);

        status = ptr.copy_array_to_user(&c, 1, idx);
        if (status != ZX_OK) {
            return status;
        }
    }
    return len.copy_to_user(idx);
}

zx_status_t sys_debug_write(user_in_ptr<const char> ptr, size_t len) {
    LTRACEF("ptr %p, len %zu\n", ptr.get(), len);

    if (len > kMaxDebugWriteSize)
        len = kMaxDebugWriteSize;

    char buf[kMaxDebugWriteSize];
    if (ptr.copy_array_from_user(buf, len) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    // This path to serial out arbitrates with the debug log
    // drainer and/or kernel ll debug path to minimize interleaving
    // of serial output between various sources
    dlog_serial_write(buf, len);

    return ZX_OK;
}

zx_status_t sys_debug_send_command(zx_handle_t handle, user_in_ptr<const char> ptr, size_t len) {
    LTRACEF("ptr %p, len %zu\n", ptr.get(), len);

    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    if (len > kMaxDebugWriteSize)
        return ZX_ERR_INVALID_ARGS;

    char buf[kMaxDebugWriteSize + 2];
    if (ptr.copy_array_from_user(buf, len) != ZX_OK)
        return ZX_ERR_INVALID_ARGS;

    buf[len] = '\n';
    buf[len + 1] = 0;
    return console_run_script(buf);
}

zx_status_t sys_ktrace_read(zx_handle_t handle, user_out_ptr<void> _data,
                            uint32_t offset, size_t len,
                            user_out_ptr<size_t> _actual) {
    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    ssize_t result = ktrace_read_user(_data.get(), offset, len);
    if (result < 0)
        return static_cast<zx_status_t>(result);

    return _actual.copy_to_user(static_cast<size_t>(result));
}

zx_status_t sys_ktrace_control(
    zx_handle_t handle, uint32_t action, uint32_t options, user_inout_ptr<void> _ptr) {
    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    switch (action) {
    case KTRACE_ACTION_NEW_PROBE: {
        char name[ZX_MAX_NAME_LEN];
        if (_ptr.copy_array_from_user(name, sizeof(name) - 1) != ZX_OK)
            return ZX_ERR_INVALID_ARGS;
        name[sizeof(name) - 1] = 0;
        return ktrace_control(action, options, name);
    }
    default:
        return ktrace_control(action, options, nullptr);
    }
}

zx_status_t sys_ktrace_write(zx_handle_t handle, uint32_t event_id, uint32_t arg0, uint32_t arg1) {
    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    if (event_id > 0x7FF) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t* args = static_cast<uint32_t*>(ktrace_open(TAG_PROBE_24(event_id)));
    if (!args) {
        //  There is not a single reason for failure. Assume it reached the end.
        return ZX_ERR_UNAVAILABLE;
    }

    args[0] = arg0;
    args[1] = arg1;
    return ZX_OK;
}

zx_status_t sys_mtrace_control(zx_handle_t handle,
                               uint32_t kind, uint32_t action, uint32_t options,
                               user_inout_ptr<void> ptr, size_t size) {
    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    return mtrace_control(kind, action, options, ptr, size);
}
