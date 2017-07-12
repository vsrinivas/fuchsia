// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include "private.h"

typedef struct vmofile {
    mxio_t io;
    mx_handle_t vmo;
    mx_off_t off;
    mx_off_t end;
    mx_off_t ptr;
    mtx_t lock;
} vmofile_t;

static ssize_t vmofile_read(mxio_t* io, void* data, size_t len) {
    vmofile_t* vf = (vmofile_t*)io;
    mx_off_t at;

    mtx_lock(&vf->lock);
    if (len > (vf->end - vf->ptr)) {
        len = vf->end - vf->ptr;
    }
    at = vf->ptr;
    vf->ptr += len;
    mtx_unlock(&vf->lock);

    mx_status_t status = mx_vmo_read(vf->vmo, data, at, len, &len);
    if (status < 0) {
        return status;
    } else {
        return len;
    }
}

static ssize_t vmofile_read_at(mxio_t* io, void* data, size_t len, off_t at) {
    vmofile_t* vf = (vmofile_t*)io;

    // make sure we're within the file's bounds
    if (at > (off_t)(vf->end - vf->off)) {
        return MX_ERR_INVALID_ARGS;
    }

    // adjust to vmo offset
    at += vf->off;

    // clip length to file bounds
    if (len > (vf->end - at)) {
        len = vf->end - at;
    }

    mx_status_t status = mx_vmo_read(vf->vmo, data, at, len, &len);
    if (status < 0) {
        return status;
    } else {
        return len;
    }
}

static ssize_t vmofile_write_at(mxio_t* io, const void* data, size_t len, off_t at) {
    return MX_ERR_NOT_SUPPORTED;
}

static off_t vmofile_seek(mxio_t* io, off_t offset, int whence) {
    vmofile_t* vf = (vmofile_t*)io;
    mtx_lock(&vf->lock);
    mx_off_t at;
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
        return MX_ERR_INVALID_ARGS;
    }
    if (at > (vf->end - vf->off)) {
        at = MX_ERR_OUT_OF_RANGE;
    } else {
        vf->ptr = vf->off + at;
    }
    mtx_unlock(&vf->lock);
    return at;
}

static mx_status_t vmofile_close(mxio_t* io) {
    vmofile_t* vf = (vmofile_t*)io;
    mx_handle_t h = vf->vmo;
    vf->vmo = 0;
    mx_handle_close(h);
    return 0;
}

static void vmofile_release(mxio_t* io) {
    vmofile_t* vf = (vmofile_t*)io;
    mx_handle_close(vf->vmo);
    free(io);
}

static mx_status_t vmofile_misc(mxio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* ptr, size_t len) {
    vmofile_t* vf = (vmofile_t*)io;
    switch (op) {
    case MXRIO_STAT: {
        vnattr_t attr;
        memset(&attr, 0, sizeof(attr));
        attr.size = vf->end - vf->off;
        attr.mode = V_TYPE_FILE | V_IRUSR;
        if (maxreply < sizeof(attr)) {
            return MX_ERR_INVALID_ARGS;
        }
        memcpy(ptr, &attr, sizeof(attr));
        return sizeof(attr);
    }
    case MXRIO_MMAP: {
        if (len != sizeof(mxrio_mmap_data_t) || maxreply < sizeof(mxrio_mmap_data_t)) {
            return MX_ERR_INVALID_ARGS;
        }
        mxrio_mmap_data_t* data = ptr;
        mx_rights_t rights = MX_RIGHT_TRANSFER | MX_RIGHT_MAP |
                             MX_RIGHT_DUPLICATE | MX_RIGHT_GET_PROPERTY;
        if (data->flags & MXIO_MMAP_FLAG_WRITE) {
            return MX_ERR_ACCESS_DENIED;
        }
        rights |= (data->flags & MXIO_MMAP_FLAG_READ) ? MX_RIGHT_READ : 0;
        rights |= (data->flags & MXIO_MMAP_FLAG_EXEC) ? MX_RIGHT_EXECUTE : 0;

        // Make a tiny clone of the portion of the portion of the VMO representing this file
        mx_handle_t h;
        // TODO(smklein): In the future, "vf->vmo" will already be a cloned vmo
        // representing this file (logically, making "vf->off" always zero), and
        // nothing past "vf->end". As a consequence, we will be able to
        // duplicate "vf->vmo" instead of cloning it.
        mx_status_t status = mx_vmo_clone(vf->vmo, MX_VMO_CLONE_COPY_ON_WRITE,
                                          vf->off, vf->end - vf->off, &h);
        if (status != MX_OK) {
            return status;
        }
        // Only return this clone with the requested rights
        mx_handle_t out;
        if ((status = mx_handle_replace(h, rights, &out)) != MX_OK) {
            mx_handle_close(h);
            return status;
        }
        return out;
    }
    case MXRIO_FCNTL: {
        uint32_t cmd = maxreply;
        switch (cmd) {
        case F_GETFL: {
            uint32_t* flags = (uint32_t*) ptr;
            if (flags) {
                *flags = 0;
            }
        }
        case F_SETFL:
            return MX_OK;
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
    }
    default:
        return MX_ERR_INVALID_ARGS;
    }
}

mx_status_t vmofile_get_vmo(mxio_t* io, mx_handle_t* out, size_t* off, size_t* len) {
    vmofile_t* vf = (vmofile_t*)io;

    if ((out == NULL) || (off == NULL) || (len == NULL)) {
        return MX_ERR_INVALID_ARGS;
    }

    *off = vf->off;
    *len = vf->end - vf->off;
    return mx_handle_duplicate(vf->vmo, MX_RIGHT_SAME_RIGHTS, out);
}

static mxio_ops_t vmofile_ops = {
    .read = vmofile_read,
    .read_at = vmofile_read_at,
    .write = mxio_default_write,
    .write_at = vmofile_write_at,
    .recvfrom = mxio_default_recvfrom,
    .sendto = mxio_default_sendto,
    .recvmsg = mxio_default_recvmsg,
    .sendmsg = mxio_default_sendmsg,
    .seek = vmofile_seek,
    .misc = vmofile_misc,
    .close = vmofile_close,
    .open = mxio_default_open,
    .clone = mxio_default_clone,
    .ioctl = mxio_default_ioctl,
    .wait_begin = mxio_default_wait_begin,
    .wait_end = mxio_default_wait_end,
    .unwrap = mxio_default_unwrap,
    .shutdown = mxio_default_shutdown,
    .posix_ioctl = mxio_default_posix_ioctl,
    .get_vmo = vmofile_get_vmo,
};

mxio_t* mxio_vmofile_create(mx_handle_t h, mx_off_t off, mx_off_t len) {
    vmofile_t* vf = calloc(1, sizeof(vmofile_t));
    if (vf == NULL) {
        mx_handle_close(h);
        return NULL;
    }
    vf->io.ops = &vmofile_ops;
    vf->io.magic = MXIO_MAGIC;
    atomic_init(&vf->io.refcount, 1);
    vf->vmo = h;
    vf->off = off;
    vf->end = off + len;
    vf->ptr = off;
    mtx_init(&vf->lock, mtx_plain);
    return &vf->io;
}

int mxio_vmo_fd(mx_handle_t vmo, uint64_t offset, uint64_t length) {
    mxio_t* io;
    int fd;
    if ((io = mxio_vmofile_create(vmo, offset, length)) == NULL) {
        return -1;
    }
    if ((fd = mxio_bind_to_fd(io, -1, 0)) < 0) {
        mxio_close(io);
        mxio_release(io);
        return -1;
    }
    return fd;
}
