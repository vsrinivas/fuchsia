// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/fdio/vfs.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "private.h"

typedef struct vmofile {
    fdio_t io;
    zx_handle_t h;
    zx_handle_t vmo;
    zx_off_t off;
    zx_off_t end;
    zx_off_t ptr;
    mtx_t lock;
} vmofile_t;

static ssize_t vmofile_read(fdio_t* io, void* data, size_t len) {
    vmofile_t* vf = (vmofile_t*)io;
    zx_off_t at;

    mtx_lock(&vf->lock);
    if (len > (vf->end - vf->ptr)) {
        len = vf->end - vf->ptr;
    }
    at = vf->ptr;
    vf->ptr += len;
    mtx_unlock(&vf->lock);

    zx_status_t status = zx_vmo_read(vf->vmo, data, at, len);
    if (status < 0) {
        return status;
    } else {
        return len;
    }
}

static ssize_t vmofile_read_at(fdio_t* io, void* data, size_t len, off_t at) {
    vmofile_t* vf = (vmofile_t*)io;

    // make sure we're within the file's bounds
    if (at > (off_t)(vf->end - vf->off)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // adjust to vmo offset
    at += vf->off;

    // clip length to file bounds
    if (len > (vf->end - at)) {
        len = vf->end - at;
    }

    zx_status_t status = zx_vmo_read(vf->vmo, data, at, len);
    if (status < 0) {
        return status;
    } else {
        return len;
    }
}

static ssize_t vmofile_write_at(fdio_t* io, const void* data, size_t len, off_t at) {
    return ZX_ERR_NOT_SUPPORTED;
}

static off_t vmofile_seek(fdio_t* io, off_t offset, int whence) {
    vmofile_t* vf = (vmofile_t*)io;
    mtx_lock(&vf->lock);
    zx_off_t at;
    switch (whence) {
    case SEEK_SET:
        at = offset;
        break;
    case SEEK_CUR:
        at = (vf->ptr - vf->off) + offset;
        break;
    case SEEK_END:
        at = (vf->end - vf->off) + offset;
        break;
    default:
        mtx_unlock(&vf->lock);
        return ZX_ERR_INVALID_ARGS;
    }
    if (at > (vf->end - vf->off)) {
        at = ZX_ERR_OUT_OF_RANGE;
    } else {
        vf->ptr = vf->off + at;
    }
    mtx_unlock(&vf->lock);
    return at;
}

static zx_status_t vmofile_close(fdio_t* io) {
    vmofile_t* vf = (vmofile_t*)io;
    zx_handle_t h = vf->h;
    if (h != ZX_HANDLE_INVALID) {
        vf->h = ZX_HANDLE_INVALID;
        zx_handle_close(h);
    }
    h = vf->vmo;
    vf->vmo = ZX_HANDLE_INVALID;
    zx_handle_close(h);
    return 0;
}

static zx_status_t vmofile_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    vmofile_t* vf = (vmofile_t*)io;
    zx_handle_t h, request;
    zx_status_t status = zx_channel_create(0, &h, &request);
    if (status != ZX_OK) {
        return status;
    }
    status = fuchsia_io_NodeClone(vf->h, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, request);
    if (status != ZX_OK) {
        zx_handle_close(h);
        return status;
    }
    handles[0] = h;
    types[0] = PA_FDIO_REMOTE;
    return 1;
}

static zx_status_t vmofile_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    vmofile_t* vf = (vmofile_t*)io;
    LOG(1, "fdio: vmofile_unwrap(%p,...)\n");
    uint64_t seek = 0u;
    zx_handle_t control = ZX_HANDLE_INVALID;

    // This function should only be called from fdio_transfer_fd, which checks
    // that the dupcount is one and removes the entry from the FD table. Those
    // checks should ensure that this function has exclusive access to the
    // |vmofile_t|, but we take this lock here to maintain the invariant that
    // we never access |ptr| without holding |lock|.
    mtx_lock(&vf->lock);
    seek = vf->ptr - vf->off;
    control = vf->h;
    mtx_unlock(&vf->lock);

    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileSeek(control, seek, fuchsia_io_SeekOrigin_START,
                                         &status, &seek)) != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }

    handles[0] = control;
    types[0] = PA_FDIO_REMOTE;
    return 1;
}

static zx_status_t vmofile_get_attr(fdio_t* io, vnattr_t* attr) {
    vmofile_t* vf = (vmofile_t*)io;
    memset(attr, 0, sizeof(*attr));
    attr->size = vf->end - vf->off;
    attr->mode = V_TYPE_FILE | V_IRUSR;
    return ZX_OK;
}

static zx_status_t vmofile_get_vmo(fdio_t* io, int flags, zx_handle_t* out) {
    vmofile_t* vf = (vmofile_t*)io;

    if (out == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t len = vf->end - vf->off;
    if (flags & fuchsia_io_VMO_FLAG_PRIVATE) {
        return zx_vmo_clone(vf->vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, len, out);
    } else {
        size_t vmo_len = 0;
        if (vf->off != 0 || zx_vmo_get_size(vf->vmo, &vmo_len) != ZX_OK ||
            len != vmo_len) {
            return ZX_ERR_NOT_FOUND;
        }
        zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_GET_PROPERTY |
                ZX_RIGHT_MAP;
        rights |= (flags & fuchsia_io_VMO_FLAG_READ) ? ZX_RIGHT_READ : 0;
        rights |= (flags & fuchsia_io_VMO_FLAG_WRITE) ? ZX_RIGHT_WRITE : 0;
        rights |= (flags & fuchsia_io_VMO_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
        return zx_handle_duplicate(vf->vmo, rights, out);
    }
}

static fdio_ops_t vmofile_ops = {
    .read = vmofile_read,
    .read_at = vmofile_read_at,
    .write = fdio_default_write,
    .write_at = vmofile_write_at,
    .seek = vmofile_seek,
    .misc = fdio_default_misc,
    .close = vmofile_close,
    .open = fdio_default_open,
    .clone = vmofile_clone,
    .ioctl = fdio_default_ioctl,
    .wait_begin = fdio_default_wait_begin,
    .wait_end = fdio_default_wait_end,
    .unwrap = vmofile_unwrap,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = vmofile_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = vmofile_get_attr,
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

#define FDIO_USE_ZXIO_VMOFILE 0

fdio_t* fdio_vmofile_create(zx_handle_t h, zx_handle_t vmo,
                            zx_off_t offset, zx_off_t length, zx_off_t seek) {
#if FDIO_USE_ZXIO_VMOFILE
    (void)vmofile_ops;
    return fdio_zxio_vmofile_create(h, vmo, offset, length, seek);
#else
    vmofile_t* vf = fdio_alloc(sizeof(vmofile_t));
    if (vf == NULL) {
        zx_handle_close(h);
        return NULL;
    }
    if (seek > length)
        seek = length;
    vf->io.ops = &vmofile_ops;
    vf->io.magic = FDIO_MAGIC;
    atomic_init(&vf->io.refcount, 1);
    vf->h = h;
    vf->vmo = vmo;
    vf->off = offset;
    vf->end = offset + length;
    vf->ptr = offset + seek;
    mtx_init(&vf->lock, mtx_plain);
    return &vf->io;
#endif
}

__EXPORT
int fdio_vmo_fd(zx_handle_t vmo, uint64_t offset, uint64_t length) {
    fdio_t* io;
    int fd;
    if ((io = fdio_vmofile_create(ZX_HANDLE_INVALID, vmo, offset, length, 0u)) == NULL) {
        return -1;
    }
    if ((fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
        fdio_close(io);
        fdio_release(io);
        return -1;
    }
    return fd;
}
