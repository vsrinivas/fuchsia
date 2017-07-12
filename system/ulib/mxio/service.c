// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>

#include <magenta/errors.h>
#include <magenta/syscalls.h>

#include "private.h"
#include "unistd.h"

typedef struct {
    mxio_t io;
    mx_handle_t h;
} mxsvc_t;

static mx_status_t mxsvc_close(mxio_t* io) {
    mxsvc_t* svc = (mxsvc_t*) io;
    mx_handle_close(svc->h);
    svc->h = MX_HANDLE_INVALID;
    return MX_OK;
}

static mxio_ops_t mx_svc_ops = {
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
    .close = mxsvc_close,
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

mxio_t* mxio_service_create(mx_handle_t h) {
    mxsvc_t* svc = calloc(1, sizeof(*svc));
    if (svc == NULL) {
        mx_handle_close(h);
        return NULL;
    }
    svc->io.ops = &mx_svc_ops;
    svc->io.magic = MXIO_MAGIC;
    svc->h = h;
    atomic_init(&svc->io.refcount, 1);
    return &svc->io;
}

mx_status_t mxio_get_service_handle(int fd, mx_handle_t* out) {
    mtx_lock(&mxio_lock);
    if ((fd < 0) || (fd >= MAX_MXIO_FD) || (mxio_fdtab[fd] == NULL)) {
        mtx_unlock(&mxio_lock);
        return ERRNO(EBADF);
    }
    mxio_t* io = mxio_fdtab[fd];
    io->dupcount--;
    mxio_fdtab[fd] = NULL;
    if (io->dupcount > 0) {
        // still alive in other fdtab slots
        // this fd goes away but we can't give away the handle
        mtx_unlock(&mxio_lock);
        mxio_release(io);
        return MX_ERR_UNAVAILABLE;
    } else {
        mtx_unlock(&mxio_lock);
        int r;
        if (io->ops == &mx_svc_ops) {
            // is a service, extract handle
            mxsvc_t* svc = (mxsvc_t*) io;
            *out = svc->h;
            svc->h = MX_HANDLE_INVALID;
            r = MX_OK;
        } else {
            r = io->ops->close(io);
            mxio_release(io);
        }
        return STATUS(r);
    }
}
