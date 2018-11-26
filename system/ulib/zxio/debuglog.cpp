// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#define LOGBUF_MAX (ZX_LOG_RECORD_MAX - sizeof(zx_log_record_t))

struct zxio_debuglog_buffer {
    unsigned next;
    char pending[LOGBUF_MAX];
};

static zx_status_t zxio_debuglog_close(zxio_t* io) {
    zxio_debuglog_t* debuglog = reinterpret_cast<zxio_debuglog_t*>(io);
    zx_handle_t handle = debuglog->handle;
    debuglog->handle = ZX_HANDLE_INVALID;
    zx_handle_close(handle);
    if (debuglog->buffer != nullptr) {
        free(debuglog->buffer);
        debuglog->buffer = nullptr;
    }
    return ZX_OK;
}

static zx_status_t zxio_debuglog_write(zxio_t* io, const void* buffer, size_t capacity,
                                       size_t* out_actual) {
    zxio_debuglog_t* debuglog = reinterpret_cast<zxio_debuglog_t*>(io);

    if (debuglog->buffer == nullptr) {
        debuglog->buffer = static_cast<zxio_debuglog_buffer_t*>(
            calloc(1, sizeof(*debuglog->buffer)));
        if (debuglog->buffer == nullptr) {
            *out_actual = capacity;
            return ZX_OK;
        }
    }

    zxio_debuglog_buffer_t* outgoing = debuglog->buffer;
    const uint8_t* data = static_cast<const uint8_t*>(buffer);
    size_t n = capacity;
    while (n-- > 0u) {
        char c = *data++;
        if (c == '\n') {
            zx_debuglog_write(debuglog->handle, 0u, outgoing->pending,
                              outgoing->next);
            outgoing->next = 0u;
            continue;
        }
        if (c < ' ') {
            continue;
        }
        outgoing->pending[outgoing->next++] = c;
        if (outgoing->next == LOGBUF_MAX) {
            zx_debuglog_write(debuglog->handle, 0u, outgoing->pending,
                              outgoing->next);
            outgoing->next = 0u;
            continue;
        }
    }
    *out_actual = capacity;
    return ZX_OK;
}

static constexpr zxio_ops_t zxio_debuglog_ops = ([]() {
    zxio_ops_t ops = zxio_default_ops;
    ops.close = zxio_debuglog_close;
    ops.write = zxio_debuglog_write;
    return ops;
})();

zx_status_t zxio_debuglog_init(zxio_storage_t* storage, zx_handle_t handle) {
    zxio_debuglog_t* debuglog = reinterpret_cast<zxio_debuglog_t*>(storage);
    zxio_init(&debuglog->io, &zxio_debuglog_ops);
    debuglog->handle = handle;
    debuglog->buffer = nullptr;
    return ZX_OK;
}
