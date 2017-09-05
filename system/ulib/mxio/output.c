// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxio/io.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <threads.h>

#include <magenta/syscalls.h>

#include "private.h"

typedef struct mxio_out mxio_out_t;

struct mxio_out {
    mxio_t io;
    ssize_t (*func)(void* cookie, const void* data, size_t len);
    void* cookie;
};

static ssize_t log_write(mxio_t* io, const void* data, size_t len) {
    mxio_out_t* out = (void*)io;
    return out->func(out->cookie, data, len);
}

static mx_status_t log_close(mxio_t* io) {
    return MX_OK;
}

static mxio_ops_t out_io_ops = {
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
    .close = mxio_default_close,
    .open = mxio_default_open,
    .clone = mxio_default_clone,
    .ioctl = mxio_default_ioctl,
    .wait_begin = mxio_default_wait_begin,
    .wait_end = mxio_default_wait_end,
    .unwrap = mxio_default_unwrap,
    .shutdown = mxio_default_shutdown,
    .posix_ioctl = mxio_default_posix_ioctl,
    .get_vmo = mxio_default_get_vmo,
};

mxio_t* mxio_output_create(ssize_t (*func)(void* cookie, const void* data, size_t len),
                           void* cookie) {
    mxio_out_t* out = calloc(1, sizeof(mxio_out_t));
    if (out == NULL) {
        return NULL;
    }
    out->io.ops = &out_io_ops;
    out->io.magic = MXIO_MAGIC;
    atomic_init(&out->io.refcount, 1);
    out->func = func;
    out->cookie = cookie;
    return &out->io;
}
