// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

    return mx_vmo_read(vf->vmo, data, at, len);
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
        return ERR_INVALID_ARGS;
    }
    if (at > (vf->end - vf->off)) {
        at = ERR_OUT_OF_RANGE;
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
            return ERR_INVALID_ARGS;
        }
        memcpy(ptr, &attr, sizeof(attr));
        return sizeof(attr);
    }
    default:
        return ERR_INVALID_ARGS;
    }
}

static mxio_ops_t vmofile_ops = {
    .read = vmofile_read,
    .write = mxio_default_write,
    .seek = vmofile_seek,
    .misc = vmofile_misc,
    .close = vmofile_close,
    .open = mxio_default_open,
    .clone = mxio_default_clone,
    .wait = mxio_default_wait,
    .ioctl = mxio_default_ioctl,
};

mxio_t* mxio_vmofile_create(mx_handle_t h, mx_off_t off, mx_off_t len) {
    vmofile_t* vf = calloc(1, sizeof(vmofile_t));
    if (vf == NULL) {
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

