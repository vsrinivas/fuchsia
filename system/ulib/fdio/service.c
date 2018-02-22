// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>

#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include "private.h"
#include "unistd.h"

typedef struct {
    fdio_t io;
    zx_handle_t h;
} mxsvc_t;

static zx_status_t mxsvc_close(fdio_t* io) {
    mxsvc_t* svc = (mxsvc_t*) io;
    zx_handle_close(svc->h);
    svc->h = ZX_HANDLE_INVALID;
    return ZX_OK;
}

static fdio_ops_t zx_svc_ops = {
    .read = fdio_default_read,
    .read_at = fdio_default_read_at,
    .write = fdio_default_write,
    .write_at = fdio_default_write_at,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .seek = fdio_default_seek,
    .misc = fdio_default_misc,
    .close = mxsvc_close,
    .open = fdio_default_open,
    .clone = fdio_default_clone,
    .ioctl = fdio_default_ioctl,
    .wait_begin = fdio_default_wait_begin,
    .wait_end = fdio_default_wait_end,
    .unwrap = fdio_default_unwrap,
    .shutdown = fdio_default_shutdown,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
};

fdio_t* fdio_service_create(zx_handle_t h) {
    mxsvc_t* svc = fdio_alloc(sizeof(*svc));
    if (svc == NULL) {
        zx_handle_close(h);
        return NULL;
    }
    svc->io.ops = &zx_svc_ops;
    svc->io.magic = FDIO_MAGIC;
    svc->h = h;
    atomic_init(&svc->io.refcount, 1);
    return &svc->io;
}

zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out) {
    mtx_lock(&fdio_lock);
    if ((fd < 0) || (fd >= FDIO_MAX_FD) || (fdio_fdtab[fd] == NULL)) {
        mtx_unlock(&fdio_lock);
        return ERRNO(EBADF);
    }
    fdio_t* io = fdio_fdtab[fd];
    io->dupcount--;
    fdio_fdtab[fd] = NULL;
    if (io->dupcount > 0) {
        // still alive in other fdtab slots
        // this fd goes away but we can't give away the handle
        mtx_unlock(&fdio_lock);
        fdio_release(io);
        return ZX_ERR_UNAVAILABLE;
    } else {
        mtx_unlock(&fdio_lock);
        int r;
        if (io->ops == &zx_svc_ops) {
            // is a service, extract handle
            mxsvc_t* svc = (mxsvc_t*) io;
            *out = svc->h;
            svc->h = ZX_HANDLE_INVALID;
            r = ZX_OK;
        } else {
            r = io->ops->close(io);
            fdio_release(io);
        }
        return STATUS(r);
    }
}
