// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxio/io.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <threads.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <magenta/processargs.h>

#include "private.h"

typedef struct mxio_log mxio_log_t;
struct mxio_log {
    mxio_t io;
    mx_handle_t handle;
};

#define LOGBUF_MAX (MX_LOG_RECORD_MAX - sizeof(mx_log_record_t))

static ssize_t log_write(mxio_t* io, const void* _data, size_t len) {
    static thread_local struct {
        unsigned next;
        char data[LOGBUF_MAX];
    }* logbuf = NULL;

    mxio_log_t* log_io = (mxio_log_t*)io;

    if (logbuf == NULL) {
        if ((logbuf = calloc(1, sizeof(*logbuf))) == NULL) {
            return len;
        }
    }

    const char* data = _data;
    size_t r = len;

    while (len-- > 0) {
        char c = *data++;
        if (c == '\n') {
            mx_log_write(log_io->handle, logbuf->next, logbuf->data, 0);
            logbuf->next = 0;
            continue;
        }
        if (c < ' ') {
            continue;
        }
        logbuf->data[logbuf->next++] = c;
        if (logbuf->next == LOGBUF_MAX) {
            mx_log_write(log_io->handle, logbuf->next, logbuf->data, 0);
            logbuf->next = 0;
            continue;
        }
    }
    return r;
}

static mx_status_t log_close(mxio_t* io) {
    mxio_log_t* log_io = (mxio_log_t*)io;
    mx_handle_t h = log_io->handle;
    log_io->handle = 0;
    mx_handle_close(h);
    return MX_OK;
}

static mx_status_t log_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mxio_log_t* log_io = (mxio_log_t*)io;

    mx_status_t status = mx_handle_duplicate(log_io->handle, MX_RIGHT_SAME_RIGHTS, &handles[0]);
    if (status < 0) {
        return status;
    }
    types[0] = PA_MXIO_LOGGER;
    return 1;
}

static mxio_ops_t log_io_ops = {
    .read = mxio_default_read,
    .read_at = mxio_default_read_at,
    .write = log_write,
    .write_at = mxio_default_write_at,
    .recvfrom = mxio_default_recvfrom,
    .sendto = mxio_default_sendto,
    .recvmsg = mxio_default_recvmsg,
    .sendmsg = mxio_default_sendmsg,
    .seek = mxio_default_seek,
    .misc = mxio_default_misc,
    .close = log_close,
    .open = mxio_default_open,
    .clone = log_clone,
    .ioctl = mxio_default_ioctl,
    .wait_begin = mxio_default_wait_begin,
    .wait_end = mxio_default_wait_end,
    .unwrap = mxio_default_unwrap,
    .shutdown = mxio_default_shutdown,
    .posix_ioctl = mxio_default_posix_ioctl,
    .get_vmo = mxio_default_get_vmo,
};

mxio_t* mxio_logger_create(mx_handle_t handle) {
    mxio_log_t* log = calloc(1, sizeof(mxio_log_t));
    if (log == NULL) {
        return NULL;
    }
    log->io.ops = &log_io_ops;
    log->io.magic = MXIO_MAGIC;
    atomic_init(&log->io.refcount, 1);
    log->handle = handle;
    return &log->io;
}
