// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

static zx_status_t zxio_vmofile_release(zxio_t* io, zx_handle_t* out_handle) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(io);

    mtx_lock(&file->lock);
    uint64_t seek = file->ptr - file->off;
    zx_handle_t control = file->control;
    zx_handle_t vmo = file->vmo;
    mtx_unlock(&file->lock);

    if (control == ZX_HANDLE_INVALID) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileSeek(control, seek, fuchsia_io_SeekOrigin_START,
                                         &status, &seek)) != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }

    mtx_lock(&file->lock);
    file->vmo = ZX_HANDLE_INVALID;
    file->control = ZX_HANDLE_INVALID;
    mtx_unlock(&file->lock);

    zx_handle_close(vmo);
    *out_handle = control;
    return ZX_OK;
}

static zx_status_t zxio_vmofile_close(zxio_t* io) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(io);
    zx_handle_t control = file->control;
    if (control != ZX_HANDLE_INVALID) {
        file->control = ZX_HANDLE_INVALID;
        zx_handle_close(control);
    }
    zx_handle_t vmo = file->vmo;
    file->vmo = ZX_HANDLE_INVALID;
    zx_handle_close(vmo);
    return ZX_OK;
}

static zx_status_t zxio_vmofile_clone_async(zxio_t* io, uint32_t flags,
                                            zx_handle_t request) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(io);
    if (file->control == ZX_HANDLE_INVALID) {
        zx_handle_close(request);
        return ZX_ERR_NOT_SUPPORTED;
    }
    return fuchsia_io_NodeClone(file->control, flags, request);
}

static zx_status_t zxio_vmofile_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(io);
    memset(out_attr, 0, sizeof(*out_attr));
    out_attr->mode = S_IFREG | S_IRUSR;
    out_attr->content_size = file->end - file->off;
    return ZX_OK;
}

static zx_status_t zxio_vmofile_read(zxio_t* io, void* buffer, size_t capacity,
                                     size_t* out_actual) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(io);

    mtx_lock(&file->lock);
    if (capacity > (file->end - file->ptr)) {
        capacity = file->end - file->ptr;
    }
    zx_off_t offset = file->ptr;
    file->ptr += capacity;
    mtx_unlock(&file->lock);

    zx_status_t status = zx_vmo_read(file->vmo, buffer, offset, capacity);
    if (status == ZX_OK) {
        *out_actual = capacity;
    }
    return status;
}

static zx_status_t zxio_vmofile_read_at(zxio_t* io, size_t offset, void* buffer,
                                        size_t capacity, size_t* out_actual) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(io);

    // Make sure we're within the file's bounds.
    if (offset > file->end - file->off) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Adjust to vmo offset.
    offset += file->off;

    // Clip length to file bounds.
    if (capacity > file->end - offset) {
        capacity = file->end - offset;
    }

    zx_status_t status = zx_vmo_read(file->vmo, buffer, offset, capacity);
    if (status == ZX_OK) {
        *out_actual = capacity;
    }
    return status;
}

static zx_status_t zxio_vmofile_seek(zxio_t* io, size_t offset,
                                     zxio_seek_origin_t start,
                                     size_t* out_offset) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(io);

    mtx_lock(&file->lock);
    zx_off_t at = 0u;
    switch (start) {
    case fuchsia_io_SeekOrigin_START:
        at = offset;
        break;
    case fuchsia_io_SeekOrigin_CURRENT:
        at = (file->ptr - file->off) + offset;
        break;
    case fuchsia_io_SeekOrigin_END:
        at = (file->end - file->off) + offset;
        break;
    default:
        mtx_unlock(&file->lock);
        return ZX_ERR_INVALID_ARGS;
    }
    if (at > file->end - file->off) {
        at = ZX_ERR_OUT_OF_RANGE;
    } else {
        file->ptr = file->off + at;
    }
    mtx_unlock(&file->lock);

    *out_offset = at;
    return ZX_OK;
}

static const zxio_ops_t zxio_vmofile_ops = {
    .release = zxio_vmofile_release,
    .close = zxio_vmofile_close,
    .wait_begin = zxio_null_wait_begin,
    .wait_end = zxio_null_wait_end,
    .clone_async = zxio_vmofile_clone_async,
    .sync = zxio_null_sync,
    .attr_get = zxio_vmofile_attr_get,
    .attr_set = zxio_null_attr_set,
    .read = zxio_vmofile_read,
    .read_at = zxio_vmofile_read_at,
    .write = zxio_null_write,
    .write_at = zxio_null_write_at,
    .seek = zxio_vmofile_seek,
    .truncate = zxio_null_truncate,
    .flags_get = zxio_null_flags_get,
    .flags_set = zxio_null_flags_set,
    .vmo_get = zxio_null_vmo_get,
    .open = zxio_null_open,
    .open_async = zxio_null_open_async,
    .unlink = zxio_null_unlink,
    .token_get = zxio_null_token_get,
    .rename = zxio_null_rename,
    .link = zxio_null_link,
    .readdir = zxio_null_readdir,
    .rewind = zxio_null_rewind,
};

zx_status_t zxio_vmofile_init(zxio_vmofile_t* file, zx_handle_t control,
                              zx_handle_t vmo, zx_off_t offset, zx_off_t length,
                              zx_off_t seek) {
    zxio_init(&file->io, &zxio_vmofile_ops);
    if (seek > length)
        seek = length;
    file->control = control;
    file->vmo = vmo;
    file->off = offset;
    file->end = offset + length;
    file->ptr = offset + seek;
    mtx_init(&file->lock, mtx_plain);
    return ZX_OK;
}
