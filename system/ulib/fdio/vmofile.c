// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <fdio/io.h>
#include <fdio/util.h>

#include <fdio/remoteio.h>
#include <fdio/vfs.h>

#include "private.h"

typedef struct vmofile {
    fdio_t io;
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
    zx_handle_t h = vf->vmo;
    vf->vmo = 0;
    zx_handle_close(h);
    return 0;
}

static zx_status_t vmofile_misc(fdio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* ptr, size_t len) {
    vmofile_t* vf = (vmofile_t*)io;
    switch (op) {
    case ZXRIO_STAT: {
        vnattr_t attr;
        memset(&attr, 0, sizeof(attr));
        attr.size = vf->end - vf->off;
        attr.mode = V_TYPE_FILE | V_IRUSR;
        if (maxreply < sizeof(attr)) {
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(ptr, &attr, sizeof(attr));
        return sizeof(attr);
    }
    case ZXRIO_MMAP: {
        if (len != sizeof(zxrio_mmap_data_t) || maxreply < sizeof(zxrio_mmap_data_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zxrio_mmap_data_t* data = ptr;
        zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY;
        if (data->flags & FDIO_MMAP_FLAG_WRITE) {
            return ZX_ERR_ACCESS_DENIED;
        }
        rights |= (data->flags & FDIO_MMAP_FLAG_READ) ? ZX_RIGHT_READ : 0;
        rights |= (data->flags & FDIO_MMAP_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;

        // Make a tiny clone of the portion of the portion of the VMO representing this file
        zx_handle_t h;
        // TODO(smklein): In the future, "vf->vmo" will already be a cloned vmo
        // representing this file (logically, making "vf->off" always zero), and
        // nothing past "vf->end". As a consequence, we will be able to
        // duplicate "vf->vmo" instead of cloning it.
        zx_status_t status = zx_vmo_clone(vf->vmo, ZX_VMO_CLONE_COPY_ON_WRITE,
                                          vf->off, vf->end - vf->off, &h);
        if (status != ZX_OK) {
            return status;
        }
        // Only return this clone with the requested rights
        zx_handle_t out;
        if ((status = zx_handle_replace(h, rights, &out)) != ZX_OK) {
            zx_handle_close(h);
            return status;
        }
        return out;
    }
    case ZXRIO_FCNTL: {
        uint32_t cmd = maxreply;
        switch (cmd) {
        case F_GETFL: {
            uint32_t* flags = (uint32_t*) ptr;
            if (flags) {
                *flags = 0;
            }
        }
        case F_SETFL:
            return ZX_OK;
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t vmofile_get_vmo(fdio_t* io, int flags, zx_handle_t* out) {
    vmofile_t* vf = (vmofile_t*)io;

    if (out == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t len = vf->end - vf->off;
    if (flags & FDIO_MMAP_FLAG_PRIVATE) {
        return zx_vmo_clone(vf->vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, len, out);
    } else {
        size_t vmo_len = 0;
        if (vf->off != 0 || zx_vmo_get_size(vf->vmo, &vmo_len) != ZX_OK ||
            len != vmo_len) {
            return ZX_ERR_NOT_FOUND;
        }
        zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_GET_PROPERTY |
                ZX_RIGHT_MAP;
        rights |= (flags & FDIO_MMAP_FLAG_READ) ? ZX_RIGHT_READ : 0;
        rights |= (flags & FDIO_MMAP_FLAG_WRITE) ? ZX_RIGHT_WRITE : 0;
        rights |= (flags & FDIO_MMAP_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
        return zx_handle_duplicate(vf->vmo, rights, out);
    }
}

static fdio_ops_t vmofile_ops = {
    .read = vmofile_read,
    .read_at = vmofile_read_at,
    .write = fdio_default_write,
    .write_at = vmofile_write_at,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .seek = vmofile_seek,
    .misc = vmofile_misc,
    .close = vmofile_close,
    .open = fdio_default_open,
    .clone = fdio_default_clone,
    .ioctl = fdio_default_ioctl,
    .wait_begin = fdio_default_wait_begin,
    .wait_end = fdio_default_wait_end,
    .unwrap = fdio_default_unwrap,
    .shutdown = fdio_default_shutdown,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = vmofile_get_vmo,
};

fdio_t* fdio_vmofile_create(zx_handle_t h, zx_off_t off, zx_off_t len) {
    vmofile_t* vf = fdio_alloc(sizeof(vmofile_t));
    if (vf == NULL) {
        zx_handle_close(h);
        return NULL;
    }
    vf->io.ops = &vmofile_ops;
    vf->io.magic = FDIO_MAGIC;
    atomic_init(&vf->io.refcount, 1);
    vf->vmo = h;
    vf->off = off;
    vf->end = off + len;
    vf->ptr = off;
    mtx_init(&vf->lock, mtx_plain);
    return &vf->io;
}

int fdio_vmo_fd(zx_handle_t vmo, uint64_t offset, uint64_t length) {
    fdio_t* io;
    int fd;
    if ((io = fdio_vmofile_create(vmo, offset, length)) == NULL) {
        return -1;
    }
    if ((fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
        fdio_close(io);
        fdio_release(io);
        return -1;
    }
    return fd;
}
