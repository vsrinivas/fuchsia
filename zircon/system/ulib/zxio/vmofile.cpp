// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

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

static zx_status_t zxio_vmofile_release(zxio_t* io, zx_handle_t* out_handle) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(io);

    mtx_lock(&file->lock);
    uint64_t seek = file->ptr - file->off;
    zx_handle_t control = file->control;
    zx_handle_t vmo = file->vmo;
    mtx_unlock(&file->lock);

    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileSeek(control, seek, fuchsia_io_SeekOrigin_START,
                                         &status, &seek)) != ZX_OK) {
        return ZX_ERR_BAD_STATE;
    }
    if (status != ZX_OK) {
        return ZX_ERR_BAD_STATE;
    }

    mtx_lock(&file->lock);
    file->vmo = ZX_HANDLE_INVALID;
    file->control = ZX_HANDLE_INVALID;
    mtx_unlock(&file->lock);

    zx_handle_close(vmo);
    *out_handle = control;
    return ZX_OK;
}

static zx_status_t zxio_vmofile_clone(zxio_t* io, zx_handle_t* out_handle) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(io);
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
        return status;
    }
    // TODO(yifeit): Switch to fuchsia_io_CLONE_FLAG_SAME_RIGHTS
    // once all vfs implementations speak the hierarchical concepts.
    uint32_t flags = fuchsia_io_OPEN_RIGHT_READABLE | fuchsia_io_OPEN_RIGHT_WRITABLE |
                     fuchsia_io_CLONE_FLAG_SAME_RIGHTS;
    status = fuchsia_io_NodeClone(file->control, flags, remote.release());
    if (status != ZX_OK) {
        return status;
    }
    *out_handle = local.release();
    return ZX_OK;
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

static constexpr zxio_ops_t zxio_vmofile_ops = []() {
    zxio_ops_t ops = zxio_default_ops;
    ops.close = zxio_vmofile_close;
    ops.release = zxio_vmofile_release;
    ops.clone = zxio_vmofile_clone;
    ops.attr_get = zxio_vmofile_attr_get;
    ops.read = zxio_vmofile_read;
    ops.read_at = zxio_vmofile_read_at;
    ops.seek = zxio_vmofile_seek;
    return ops;
}();

zx_status_t zxio_vmofile_init(zxio_storage_t* storage, zx_handle_t control,
                              zx_handle_t vmo, zx_off_t offset, zx_off_t length,
                              zx_off_t seek) {
    zxio_vmofile_t* file = reinterpret_cast<zxio_vmofile_t*>(storage);
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
