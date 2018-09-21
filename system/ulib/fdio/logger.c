// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/io.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <threads.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/processargs.h>

#include "private.h"

typedef struct fdio_log fdio_log_t;
struct fdio_log {
    fdio_t io;
    zx_handle_t handle;
};

#define LOGBUF_MAX (ZX_LOG_RECORD_MAX - sizeof(zx_log_record_t))

static ssize_t log_write(fdio_t* io, const void* _data, size_t len) {
    static thread_local struct {
        unsigned next;
        char data[LOGBUF_MAX];
    }* logbuf = NULL;

    fdio_log_t* log_io = (fdio_log_t*)io;

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
            zx_debuglog_write(log_io->handle, 0, logbuf->data, logbuf->next);
            logbuf->next = 0;
            continue;
        }
        if (c < ' ') {
            continue;
        }
        logbuf->data[logbuf->next++] = c;
        if (logbuf->next == LOGBUF_MAX) {
            zx_debuglog_write(log_io->handle, 0, logbuf->data, logbuf->next);
            logbuf->next = 0;
            continue;
        }
    }
    return r;
}

static zx_status_t log_close(fdio_t* io) {
    fdio_log_t* log_io = (fdio_log_t*)io;
    zx_handle_t h = log_io->handle;
    log_io->handle = 0;
    zx_handle_close(h);
    return ZX_OK;
}

static zx_status_t log_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    fdio_log_t* log_io = (fdio_log_t*)io;

    zx_status_t status = zx_handle_duplicate(log_io->handle, ZX_RIGHT_SAME_RIGHTS, &handles[0]);
    if (status < 0) {
        return status;
    }
    types[0] = PA_FDIO_LOGGER;
    return 1;
}

static fdio_ops_t log_io_ops = {
    .read = fdio_default_read,
    .read_at = fdio_default_read_at,
    .write = log_write,
    .write_at = fdio_default_write_at,
    .seek = fdio_default_seek,
    .misc = fdio_default_misc,
    .close = log_close,
    .open = fdio_default_open,
    .clone = log_clone,
    .ioctl = fdio_default_ioctl,
    .wait_begin = fdio_default_wait_begin,
    .wait_end = fdio_default_wait_end,
    .unwrap = fdio_default_unwrap,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
    .sync = fdio_default_sync,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_default_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .shutdown = fdio_default_shutdown,
};

__EXPORT
fdio_t* fdio_logger_create(zx_handle_t handle) {
    fdio_log_t* log = calloc(1, sizeof(fdio_log_t));
    if (log == NULL) {
        return NULL;
    }
    log->io.ops = &log_io_ops;
    log->io.magic = FDIO_MAGIC;
    atomic_init(&log->io.refcount, 1);
    log->handle = handle;
    return &log->io;
}
