// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <mxio/io.h>

#include <stdlib.h>

#include <magenta/syscalls.h>

#include <runtime/mutex.h>
#include <runtime/tls.h>

#include "private.h"

static int logbuf_tls = -1;
static mxr_mutex_t logbuf_lock;

typedef struct mxio_log mxio_log_t;
struct mxio_log {
    mxio_t io;
    mx_handle_t handle;
};

#define LOGBUF_MAX (MX_LOG_RECORD_MAX - sizeof(mx_log_record_t))

typedef struct ciobuf {
    uint next;
    char data[LOGBUF_MAX];
} logbuf_t;

static ssize_t log_write(mxio_t* io, const void* _data, size_t len) {
    mxio_log_t* log_io = (mxio_log_t*)io;
    logbuf_t* log = mxr_tls_get(logbuf_tls);

    if (log == NULL) {
        if ((log = calloc(1, sizeof(logbuf_t))) == NULL) {
            return len;
        }
        mxr_tls_set(logbuf_tls, log);
    }

    const char* data = _data;
    size_t r = len;

    while (len-- > 0) {
        char c = *data++;
        if (c == '\n') {
            _magenta_log_write(log_io->handle, log->next, log->data, 0);
            log->next = 0;
            continue;
        }
        if (c < ' ') {
            continue;
        }
        log->data[log->next++] = c;
        if (log->next == LOGBUF_MAX) {
            _magenta_log_write(log_io->handle, log->next, log->data, 0);
            log->next = 0;
            continue;
        }
    }
    return r;
}

static mx_status_t log_close(mxio_t* io) {
    mxio_log_t* log_io = (mxio_log_t*)io;
    mx_handle_t h = log_io->handle;
    log_io->handle = 0;
    _magenta_handle_close(h);
    return NO_ERROR;
}

static mxio_ops_t log_io_ops = {
    .read = mxio_default_read,
    .write = log_write,
    .seek = mxio_default_seek,
    .misc = mxio_default_misc,
    .close = log_close,
    .open = mxio_default_open,
    .clone = mxio_default_clone,
    .wait = mxio_default_wait,
    .ioctl = mxio_default_ioctl,
};

mxio_t* mxio_logger_create(mx_handle_t handle) {
    mxr_mutex_lock(&logbuf_lock);
    if (logbuf_tls < 0) {
        logbuf_tls = mxr_tls_allocate();
    }
    mxr_mutex_unlock(&logbuf_lock);
    if (logbuf_tls < 0) {
        return NULL;
    }
    mxio_log_t* log = calloc(1, sizeof(mxio_log_t));
    if (log == NULL) {
        return NULL;
    }
    log->io.ops = &log_io_ops;
    log->io.magic = MXIO_MAGIC;
    log->io.refcount = 1;
    log->handle = handle;
    return &log->io;
}
