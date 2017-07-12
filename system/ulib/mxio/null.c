// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
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

ssize_t mxio_default_read_at(mxio_t* io, void* _data, size_t len, off_t offset) {
    return MX_ERR_WRONG_TYPE;
}

ssize_t mxio_default_write(mxio_t* io, const void* _data, size_t len) {
    return len;
}

ssize_t mxio_default_write_at(mxio_t* io, const void* _data, size_t len, off_t offset) {
    return MX_ERR_WRONG_TYPE;
}

ssize_t mxio_default_recvfrom(mxio_t* io, void* data, size_t len, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    return MX_ERR_WRONG_TYPE;
}

ssize_t mxio_default_sendto(mxio_t* io, const void* data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    return MX_ERR_WRONG_TYPE;
}

ssize_t mxio_default_recvmsg(mxio_t* io, struct msghdr* msg, int flags) {
    return MX_ERR_WRONG_TYPE;
}

ssize_t mxio_default_sendmsg(mxio_t* io, const struct msghdr* msg, int flags) {
    return MX_ERR_WRONG_TYPE;
}

off_t mxio_default_seek(mxio_t* io, off_t offset, int whence) {
    return MX_ERR_WRONG_TYPE;
}

mx_status_t mxio_default_misc(mxio_t* io, uint32_t op, int64_t off, uint32_t arg, void* data, size_t len) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t mxio_default_open(mxio_t* io, const char* path, int32_t flags, uint32_t mode, mxio_t** out) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t mxio_default_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t mxio_default_unwrap(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t mxio_default_shutdown(mxio_t* io, int how) {
    return MX_ERR_WRONG_TYPE;
}

mx_status_t mxio_default_close(mxio_t* io) {
    return MX_OK;
}

ssize_t mxio_default_ioctl(mxio_t* io, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    return MX_ERR_NOT_SUPPORTED;
}

void mxio_default_wait_begin(mxio_t* io, uint32_t events,
                             mx_handle_t* handle, mx_signals_t* _signals) {
    *handle = MX_HANDLE_INVALID;
}

void mxio_default_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
}

ssize_t mxio_default_posix_ioctl(mxio_t* io, int req, va_list va) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t mxio_default_get_vmo(mxio_t* io, mx_handle_t* out, size_t* off, size_t* len) {
    return MX_ERR_NOT_SUPPORTED;
}

static mxio_ops_t mx_null_ops = {
    .read = mxio_default_read,
    .read_at = mxio_default_read_at,
    .write = mxio_default_write,
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
