// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <string.h>
#include <zircon/syscalls.h>

#define ZXIO_REMOTE_CHUNK_SIZE 8192

static zx_status_t zxio_remote_release(zxio_t* io, zx_handle_t* out_handle) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_handle_t control = rio->control;
    rio->control = ZX_HANDLE_INVALID;
    if (rio->event != ZX_HANDLE_INVALID) {
        zx_handle_t event = rio->event;
        rio->event = ZX_HANDLE_INVALID;
        zx_handle_close(event);
    }
    *out_handle = control;
    return ZX_OK;
}

static zx_status_t zxio_remote_close(zxio_t* io) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_NodeClose(rio->control, &status);
    zx_handle_t control = rio->control;
    rio->control = ZX_HANDLE_INVALID;
    zx_handle_close(control);
    if (rio->event != ZX_HANDLE_INVALID) {
        zx_handle_t event = rio->event;
        rio->event = ZX_HANDLE_INVALID;
        zx_handle_close(event);
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

static zx_status_t zxio_remote_read_once(zxio_remote_t* rio, uint8_t* buffer,
                                         size_t capacity, size_t* out_actual) {
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileRead(rio->control, capacity, &status,
                                    buffer, capacity, &actual);
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

static zx_status_t zxio_remote_read(zxio_t* io, void* data, size_t capacity,
                                    size_t* out_actual) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    uint8_t* buffer = static_cast<uint8_t*>(data);
    size_t received = 0;
    while (capacity > 0) {
        size_t chunk = (capacity > ZXIO_REMOTE_CHUNK_SIZE) ? ZXIO_REMOTE_CHUNK_SIZE : capacity;
        size_t actual = 0;
        zx_status_t status = zxio_remote_read_once(rio, buffer, chunk, &actual);
        if (status != ZX_OK) {
            return status;
        }
        received += actual;
        buffer += actual;
        capacity -= actual;
        if (chunk != actual) {
            break;
        }
    }
    *out_actual = received;
    return ZX_OK;
}

static zx_status_t zxio_remote_read_once_at(zxio_remote_t* rio, size_t offset,
                                            uint8_t* buffer, size_t capacity,
                                            size_t* out_actual) {
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileReadAt(rio->control, capacity, offset, &status,
                                      buffer, capacity, &actual);
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

static zx_status_t zxio_remote_read_at(zxio_t* io, size_t offset, void* data,
                                       size_t capacity, size_t* out_actual) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    uint8_t* buffer = static_cast<uint8_t*>(data);
    size_t received = 0;
    while (capacity > 0) {
        size_t chunk = (capacity > ZXIO_REMOTE_CHUNK_SIZE) ? ZXIO_REMOTE_CHUNK_SIZE : capacity;
        size_t actual = 0;
        zx_status_t status = zxio_remote_read_once_at(rio, offset, buffer,
                                                      chunk, &actual);
        if (status != ZX_OK) {
            return status;
        }
        offset += actual;
        received += actual;
        buffer += actual;
        capacity -= actual;
        if (chunk != actual) {
            break;
        }
    }
    *out_actual = received;
    return ZX_OK;
}

static zx_status_t zxio_remote_write_once(zxio_remote_t* rio, const uint8_t* buffer,
                                          size_t capacity, size_t* out_actual) {
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileWrite(rio->control, buffer, capacity, &status,
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

static zx_status_t zxio_remote_write(zxio_t* io, const void* data,
                                     size_t capacity, size_t* out_actual) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    const uint8_t* buffer = static_cast<const uint8_t*>(data);
    size_t sent = 0u;
    while (capacity > 0) {
        size_t chunk = (capacity > ZXIO_REMOTE_CHUNK_SIZE) ? ZXIO_REMOTE_CHUNK_SIZE : capacity;
        size_t actual = 0u;
        zx_status_t status = zxio_remote_write_once(rio, buffer, chunk,
                                                    &actual);
        if (status != ZX_OK) {
            return status;
        }
        sent += actual;
        buffer += actual;
        capacity -= actual;
        if (chunk != actual) {
            break;
        }
    }
    *out_actual = sent;
    return ZX_OK;
}

static zx_status_t zxio_remote_write_once_at(zxio_remote_t* rio, size_t offset,
                                             const uint8_t* buffer, size_t capacity,
                                             size_t* out_actual) {
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileWriteAt(rio->control, buffer, capacity, offset,
                                       &status, &actual);
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

static zx_status_t zxio_remote_write_at(zxio_t* io, size_t offset,
                                        const void* data, size_t capacity,
                                        size_t* out_actual) {
    zxio_remote_t* rio = reinterpret_cast<zxio_remote_t*>(io);
    const uint8_t* buffer = static_cast<const uint8_t*>(data);
    size_t sent = 0u;
    while (capacity > 0) {
        size_t chunk = (capacity > ZXIO_REMOTE_CHUNK_SIZE) ? ZXIO_REMOTE_CHUNK_SIZE : capacity;
        size_t actual = 0u;
        zx_status_t status = zxio_remote_write_once_at(rio, offset, buffer,
                                                       chunk, &actual);
        if (status != ZX_OK) {
            return status;
        }
        sent += actual;
        buffer += actual;
        offset += actual;
        capacity -= actual;
        if (chunk != actual) {
            break;
        }
    }
    *out_actual = sent;
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
    *out_size = 0u;
    return ZX_OK;
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

static constexpr zxio_ops_t zxio_remote_ops = []() {
    zxio_ops_t ops = zxio_default_ops;
    ops.release = zxio_remote_release;
    ops.close = zxio_remote_close;
    ops.clone_async = zxio_remote_clone_async;
    ops.sync = zxio_remote_sync;
    ops.attr_get = zxio_remote_attr_get;
    ops.attr_set = zxio_remote_attr_set;
    ops.read = zxio_remote_read;
    ops.read_at = zxio_remote_read_at;
    ops.write = zxio_remote_write;
    ops.write_at = zxio_remote_write_at;
    ops.seek = zxio_remote_seek;
    ops.truncate = zxio_remote_truncate;
    ops.flags_get = zxio_remote_flags_get;
    ops.flags_set = zxio_remote_flags_set;
    ops.vmo_get = zxio_remote_vmo_get;
    ops.open_async = zxio_remote_open_async;
    ops.unlink = zxio_remote_unlink;
    ops.token_get = zxio_remote_token_get;
    ops.rename = zxio_remote_rename;
    ops.link = zxio_remote_link;
    ops.readdir = zxio_remote_readdir;
    ops.rewind = zxio_remote_rewind;
    return ops;
}();

zx_status_t zxio_remote_init(zxio_storage_t* storage, zx_handle_t control,
                             zx_handle_t event) {
    zxio_remote_t* remote = reinterpret_cast<zxio_remote_t*>(storage);
    zxio_init(&remote->io, &zxio_remote_ops);
    remote->control = control;
    remote->event = event;
    return ZX_OK;
}
