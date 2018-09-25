// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>
#include <string.h>
#include <zircon/syscalls.h>

static zx_status_t zxio_remote_release(zxio_t* io, zx_handle_t* out_node) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_handle_t node = rio->control;
    rio->control = ZX_HANDLE_INVALID;
    if (rio->event != ZX_HANDLE_INVALID) {
        zx_handle_t h = rio->event;
        rio->event = ZX_HANDLE_INVALID;
        zx_handle_close(h);
    }
    *out_node = node;
    return ZX_OK;
}

static zx_status_t zxio_remote_close(zxio_t* io) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_NodeClose(rio->control, &status);
    zx_handle_t h = rio->control;
    rio->control = ZX_HANDLE_INVALID;
    zx_handle_close(h);
    if (rio->event != ZX_HANDLE_INVALID) {
        zx_handle_t h = rio->event;
        rio->event = ZX_HANDLE_INVALID;
        zx_handle_close(h);
    }
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_clone_async(zxio_t* io, uint32_t flags, zx_handle_t request) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    return fuchsia_io_NodeClone(rio->control, flags, request);
}

static zx_status_t zxio_remote_sync(zxio_t* io) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_NodeSync(rio->control, &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_NodeGetAttr(rio->control, &status, out_attr);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_attr_set(zxio_t* io, uint32_t flags, const zxio_node_attr_t* attr) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_NodeSetAttr(rio->control, flags, attr, &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_read(zxio_t* io, void* buffer, size_t capacity, size_t* out_actual) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileRead(rio->control, capacity, &status,
                                    static_cast<uint8_t*>(buffer), capacity,
                                    &actual);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (actual > capacity) {
        return ZX_ERR_IO;
    }
    *out_actual = actual;
    return ZX_OK;
}

static zx_status_t zxio_remote_read_at(zxio_t* io, size_t offset, void* buffer, size_t capacity, size_t* out_actual) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileReadAt(rio->control, capacity, offset, &status,
                                      static_cast<uint8_t*>(buffer), capacity,
                                      &actual);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (actual > capacity) {
        return ZX_ERR_IO;
    }
    *out_actual = actual;
    return ZX_OK;
}

static zx_status_t zxio_remote_write(zxio_t* io, const void* buffer, size_t capacity, size_t* out_actual) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileWrite(rio->control, static_cast<const uint8_t*>(buffer),
                                          capacity, &status, &actual);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (actual > capacity) {
        return ZX_ERR_IO;
    }
    *out_actual = actual;
    return ZX_OK;
}

static zx_status_t zxio_remote_write_at(zxio_t* io, size_t offset, const void* buffer, size_t capacity, size_t* out_actual) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileWriteAt(rio->control, static_cast<const uint8_t*>(buffer),
                                       capacity, offset, &status, &actual);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (actual > capacity) {
        return ZX_ERR_IO;
    }
    *out_actual = actual;
    return ZX_OK;
}

static zx_status_t zxio_remote_seek(zxio_t* io, size_t offset, zxio_seek_origin_t start, size_t* out_offset) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileSeek(rio->control, offset, start, &status, out_offset);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_truncate(zxio_t* io, size_t length) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileTruncate(rio->control, length, &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_flags_get(zxio_t* io, uint32_t* out_flags) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileGetFlags(rio->control, &status, out_flags);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_flags_set(zxio_t* io, uint32_t flags) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileSetFlags(rio->control, flags, &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo, size_t* out_size) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_handle_t vmo = ZX_HANDLE_INVALID;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileGetVmo(rio->control, flags, &status, &vmo);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (vmo == ZX_HANDLE_INVALID) {
        return ZX_ERR_IO;
    }
    *out_vmo = vmo;
    return ZX_OK;
}

static zx_status_t zxio_remote_open(zxio_t* io, uint32_t flags, uint32_t mode, const char* path, zxio_t** out_io) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t zxio_remote_open_async(zxio_t* io, uint32_t flags, uint32_t mode, const char* path, zx_handle_t request) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    return fuchsia_io_DirectoryOpen(rio->control, flags, mode, path, strlen(path), request);
}

static zx_status_t zxio_remote_unlink(zxio_t* io, const char* path) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryUnlink(rio->control, path, strlen(path), &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_token_get(zxio_t* io, zx_handle_t* out_token) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryGetToken(rio->control, &status, out_token);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_rename(zxio_t* io, const char* src_path, zx_handle_t dst_token, const char* dst_path) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryRename(rio->control, src_path, strlen(src_path), dst_token,
                                           dst_path, strlen(dst_path), &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_link(zxio_t* io, const char* src_path, zx_handle_t dst_token, const char* dst_path) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryLink(rio->control, src_path, strlen(src_path), dst_token,
                                         dst_path, strlen(dst_path), &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t zxio_remote_readdir(zxio_t* io, void* buffer, size_t capacity, size_t* out_actual) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryReadDirents(rio->control, capacity, &status,
                                                static_cast<uint8_t*>(buffer),
                                                capacity, &actual);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (actual > capacity) {
        return ZX_ERR_IO;
    }
    *out_actual = actual;
    return status;
}

static zx_status_t zxio_remote_rewind(zxio_t* io) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryRewind(rio->control, &status);
    return io_status != ZX_OK ? io_status : status;
}

static const zxio_ops_t zxio_remote_ops = {
    .release = zxio_remote_release,
    .close = zxio_remote_close,
    .clone_async = zxio_remote_clone_async,
    .sync = zxio_remote_sync,
    .attr_get = zxio_remote_attr_get,
    .attr_set = zxio_remote_attr_set,
    .read = zxio_remote_read,
    .read_at = zxio_remote_read_at,
    .write = zxio_remote_write,
    .write_at = zxio_remote_write_at,
    .seek = zxio_remote_seek,
    .truncate = zxio_remote_truncate,
    .flags_get = zxio_remote_flags_get,
    .flags_set = zxio_remote_flags_set,
    .vmo_get = zxio_remote_vmo_get,
    .open = zxio_remote_open,
    .open_async = zxio_remote_open_async,
    .unlink = zxio_remote_unlink,
    .token_get = zxio_remote_token_get,
    .rename = zxio_remote_rename,
    .link = zxio_remote_link,
    .readdir = zxio_remote_readdir,
    .rewind = zxio_remote_rewind,
};

zx_status_t zxio_remote_init(zxio_remote_t* rio, zx_handle_t control,
                             zx_handle_t event) {
    zxio_init(&rio->io, &zxio_remote_ops);
    rio->control = control;
    rio->event = event;
    return ZX_OK;
}
