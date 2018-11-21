// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>

#include <lib/fdio/unsafe.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include "private.h"
#include "private-remoteio.h"
#include "unistd.h"

typedef struct {
    fdio_t io;
    zx_handle_t h;
} zxsvc_t;

static zx_status_t zxsvc_close(fdio_t* io) {
    zxsvc_t* svc = (zxsvc_t*) io;
    zx_handle_close(svc->h);
    svc->h = ZX_HANDLE_INVALID;
    return ZX_OK;
}

static fdio_ops_t zx_svc_ops = {
    .read = fdio_default_read,
    .read_at = fdio_default_read_at,
    .write = fdio_default_write,
    .write_at = fdio_default_write_at,
    .seek = fdio_default_seek,
    .misc = fdio_default_misc,
    .close = zxsvc_close,
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
fdio_t* fdio_service_create(zx_handle_t h) {
    zxsvc_t* svc = fdio_alloc(sizeof(*svc));
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

__EXPORT
zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out) {
    mtx_lock(&fdio_lock);
    if ((fd < 0) || (fd >= FDIO_MAX_FD) || (fdio_fdtab[fd] == NULL)) {
        mtx_unlock(&fdio_lock);
        return ZX_ERR_NOT_FOUND;
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
        zx_status_t r;
        if (io->ops == &zx_svc_ops) {
            // is an unknown service, extract handle
            zxsvc_t* svc = (zxsvc_t*) io;
            *out = svc->h;
            svc->h = ZX_HANDLE_INVALID;
            r = ZX_OK;
        } else if (io->ops == &fdio_zxio_remote_ops) {
            zxio_remote_t* file = fdio_get_zxio_remote(io);
            r = zxio_release(&file->io, out);
        } else {
            r = ZX_ERR_NOT_SUPPORTED;
            io->ops->close(io);
        }
        fdio_release(io);
        return r;
    }
}

__EXPORT
zx_handle_t fdio_unsafe_borrow_channel(fdio_t* io) {
    if (io == NULL) {
        return ZX_HANDLE_INVALID;
    }

    if (io->ops == &zx_svc_ops) {
        zxsvc_t* svc = (zxsvc_t*) io;
        return svc->h;
    } else if (io->ops == &fdio_zxio_remote_ops) {
        zxio_remote_t* file = fdio_get_zxio_remote(io);
        return file->control;
    }
    return ZX_HANDLE_INVALID;
}
