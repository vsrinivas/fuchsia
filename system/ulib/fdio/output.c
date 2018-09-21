// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/io.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <threads.h>

#include <zircon/syscalls.h>

#include "private.h"

typedef struct fdio_out fdio_out_t;

struct fdio_out {
    fdio_t io;
    ssize_t (*func)(void* cookie, const void* data, size_t len);
    void* cookie;
};

static ssize_t log_write(fdio_t* io, const void* data, size_t len) {
    fdio_out_t* out = (void*)io;
    return out->func(out->cookie, data, len);
}

static zx_status_t log_close(fdio_t* io) {
    return ZX_OK;
}

static fdio_ops_t out_io_ops = {
    .read = fdio_default_read,
    .read_at = fdio_default_read_at,
    .write = log_write,
    .write_at = fdio_default_write_at,
    .seek = fdio_default_seek,
    .misc = fdio_default_misc,
    .close = fdio_default_close,
    .open = fdio_default_open,
    .clone = fdio_default_clone,
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
fdio_t* fdio_output_create(ssize_t (*func)(void* cookie, const void* data, size_t len),
                           void* cookie) {
    fdio_out_t* out = fdio_alloc(sizeof(fdio_out_t));
    if (out == NULL) {
        return NULL;
    }
    out->io.ops = &out_io_ops;
    out->io.magic = FDIO_MAGIC;
    atomic_init(&out->io.refcount, 1);
    out->func = func;
    out->cookie = cookie;
    return &out->io;
}
