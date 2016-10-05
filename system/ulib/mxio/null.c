// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/types.h>
#include <mxio/io.h>

#include "private.h"

void mxio_free(mxio_t* io) {
    io->magic = 0xDEAD0123;
    io->ops = NULL;
    free(io);
}

ssize_t mxio_default_read(mxio_t* io, void* _data, size_t len) {
    return 0;
}

ssize_t mxio_default_write(mxio_t* io, const void* _data, size_t len) {
    return len;
}

off_t mxio_default_seek(mxio_t* io, off_t offset, int whence) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t mxio_default_misc(mxio_t* io, uint32_t op, int64_t off, uint32_t arg, void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t mxio_default_open(mxio_t* io, const char* path, int32_t flags, uint32_t mode, mxio_t** out) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t mxio_default_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t mxio_default_wait(mxio_t* io, uint32_t events, uint32_t* pending, mx_time_t timeout) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t mxio_default_close(mxio_t* io) {
    return NO_ERROR;
}

ssize_t mxio_default_ioctl(mxio_t* io, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    return ERR_NOT_SUPPORTED;
}

static mxio_ops_t mx_null_ops = {
    .read = mxio_default_read,
    .write = mxio_default_write,
    .seek = mxio_default_seek,
    .misc = mxio_default_misc,
    .close = mxio_default_close,
    .open = mxio_default_open,
    .clone = mxio_default_clone,
    .wait = mxio_default_wait,
    .ioctl = mxio_default_ioctl,
};

mxio_t* mxio_null_create(void) {
    mxio_t* io = calloc(1, sizeof(*io));
    if (io == NULL) {
        return NULL;
    }
    io->ops = &mx_null_ops;
    io->magic = MXIO_MAGIC;
    atomic_init(&io->refcount, 1);
    return io;
}
