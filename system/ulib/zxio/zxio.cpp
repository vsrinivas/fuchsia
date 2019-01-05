// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>

// The private fields of a |zxio_t| object.
//
// In |ops.h|, the |zxio_t| struct is defined as opaque. Clients of the zxio
// library are forbidden from relying upon the structure of |zxio_t| objects.
// To avoid temptation, the details of the structure are defined only in this
// implementation file and are not visible in the header.
typedef struct zxio_internal {
    const zxio_ops_t* ops;
    uint64_t reserved[3];
} zxio_internal_t;

static_assert(sizeof(zxio_t) == sizeof(zxio_internal_t),
              "zxio_t should match zxio_internal_t");

static_assert(ZXIO_READABLE == __ZX_OBJECT_READABLE,
              "ZXIO signal bits should match ZX");
static_assert(ZXIO_WRITABLE == __ZX_OBJECT_WRITABLE,
              "ZXIO signal bits should match ZX");
static_assert(ZXIO_READ_DISABLED == ZX_SOCKET_PEER_WRITE_DISABLED,
              "ZXIO signal bits should match ZX");
static_assert(ZXIO_WRITE_DISABLED == ZX_SOCKET_WRITE_DISABLED,
              "ZXIO signal bits should match ZX");
static_assert(ZXIO_READ_THRESHOLD == ZX_SOCKET_READ_THRESHOLD,
              "ZXIO signal bits should match ZX");
static_assert(ZXIO_WRITE_THRESHOLD == ZX_SOCKET_WRITE_THRESHOLD,
              "ZXIO signal bits should match ZX");

void zxio_init(zxio_t* io, const zxio_ops_t* ops) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    memset(zio, 0, sizeof(*zio));
    zio->ops = ops;
}

zx_status_t zxio_acquire_node(zx_handle_t node, zxio_t** out_io) {
    zx_handle_close(node);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_acquire_socket(zx_handle_t socket, zxio_t** out_io) {
    zx_handle_close(socket);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_release(zxio_t* io, zx_handle_t* out_io) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->release(io, out_io);
}

zx_status_t zxio_close(zxio_t* io) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->close(io);
}

zx_status_t zxio_wait_one(zxio_t* io, zxio_signals_t signals,
                          zx_time_t deadline, zxio_signals_t* out_observed) {
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_signals_t zx_signals = ZX_SIGNAL_NONE;
    zxio_wait_begin(io, signals, &handle, &zx_signals);
    if (handle == ZX_HANDLE_INVALID) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    zx_signals_t zx_observed = ZX_SIGNAL_NONE;
    zx_status_t status = zx_object_wait_one(handle, zx_signals, deadline,
                                            &zx_observed);
    if (status != ZX_OK) {
        return status;
    }
    zxio_wait_end(io, zx_signals, out_observed);
    return ZX_OK;
}

void zxio_wait_begin(zxio_t* io, zxio_signals_t zxio_signals,
                     zx_handle_t* out_handle, zx_signals_t* out_zx_signals) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->wait_begin(io, zxio_signals, out_handle, out_zx_signals);
}

void zxio_wait_end(zxio_t* io, zx_signals_t zx_signals,
                   zxio_signals_t* out_zxio_signals) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->wait_end(io, zx_signals, out_zxio_signals);
}

zx_status_t zxio_clone(zxio_t* io, uint32_t flags, zxio_t** out_io) {
    zx::channel h1, h2;
    zx_status_t status = zx::channel::create(0, &h1, &h2);
    if (status != ZX_OK)
        return status;
    status = zxio_clone_async(io, flags, h1.release());
    if (status != ZX_OK)
        return status;
    return zxio_acquire_node(h2.release(), out_io);
}

zx_status_t zxio_clone_async(zxio_t* io, uint32_t flags,
                             zx_handle_t request) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->clone_async(io, flags, request);
}

zx_status_t zxio_sync(zxio_t* io) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->sync(io);
}

zx_status_t zxio_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->attr_get(io, out_attr);
}

zx_status_t zxio_attr_set(zxio_t* io, uint32_t flags,
                          const zxio_node_attr_t* attr) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->attr_set(io, flags, attr);
}

zx_status_t zxio_read(zxio_t* io, void* buffer, size_t capacity,
                      size_t* out_actual) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->read(io, buffer, capacity, out_actual);
}

zx_status_t zxio_read_at(zxio_t* io, size_t offset, void* buffer,
                         size_t capacity, size_t* out_actual) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->read_at(io, offset, buffer, capacity,
                             out_actual);
}

zx_status_t zxio_write(zxio_t* io, const void* buffer, size_t capacity,
                       size_t* out_actual) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->write(io, buffer, capacity, out_actual);
}

zx_status_t zxio_write_at(zxio_t* io, size_t offset, const void* buffer,
                          size_t capacity, size_t* out_actual) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->write_at(io, offset, buffer, capacity,
                              out_actual);
}

zx_status_t zxio_seek(zxio_t* io, size_t offset,
                      zxio_seek_origin_t start, size_t* out_offset) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->seek(io, offset, start, out_offset);
}

zx_status_t zxio_truncate(zxio_t* io, size_t length) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->truncate(io, length);
}

zx_status_t zxio_flags_get(zxio_t* io, uint32_t* out_flags) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->flags_get(io, out_flags);
}

zx_status_t zxio_flags_set(zxio_t* io, uint32_t flags) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->flags_set(io, flags);
}

static zx_status_t read_at(zxio_t* io, void* buf, size_t len, off_t offset,
                           size_t* out_actual) {
    size_t actual = 0u;
    zx_status_t status = ZX_OK;
    for (;;) {
        status = zxio_read_at(io, offset, buf, len, &actual);
        if (status != ZX_ERR_SHOULD_WAIT) {
            break;
        }
        zxio_signals_t observed = ZXIO_SIGNAL_NONE;
        status = zxio_wait_one(io, ZXIO_READABLE | ZXIO_READ_DISABLED,
                               ZX_TIME_INFINITE, &observed);
        if (status != ZX_OK) {
            break;
        }
    }
    if (status != ZX_OK) {
        return status;
    }
    if (actual == 0) { // EOF (?)
        return ZX_ERR_OUT_OF_RANGE;
    }
    *out_actual = actual;
    return ZX_OK;
}

constexpr size_t kMinWindow = PAGE_SIZE * 4;
constexpr size_t kMaxWindow = (size_t)64 << 20;

static zx_status_t read_file_into_vmo(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
    auto current_vmar = zx::vmar::root_self();

    fuchsia_io_NodeAttributes attr;
    zx_status_t status = zxio_attr_get(io, &attr);
    if (status != ZX_OK) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint64_t size = attr.content_size;
    uint64_t offset = 0;

    zx::vmo vmo;
    status = zx::vmo::create(size, 0, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    while (size > 0) {
        if (size < kMinWindow) {
            // There is little enough left that copying is less overhead
            // than fiddling with the page tables.
            char buffer[PAGE_SIZE];
            size_t xfer = size < sizeof(buffer) ? size : sizeof(buffer);
            size_t nread;
            status = read_at(io, buffer, xfer, offset, &nread);
            if (status != ZX_OK) {
                return status;
            }
            status = zx_vmo_write(*out_vmo, buffer, offset, nread);
            if (status != ZX_OK) {
                return status;
            }
            offset += nread;
            size -= nread;
        } else {
            // Map the VMO into our own address space so we can read into
            // it directly and avoid double-buffering.
            size_t chunk = size < kMaxWindow ? size : kMaxWindow;
            size_t window = (chunk + PAGE_SIZE - 1) & -PAGE_SIZE;
            uintptr_t start = 0;
            status = current_vmar->map(0, vmo, offset, window,
                                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                       &start);
            if (status != ZX_OK) {
                return status;
            }
            uint8_t* buffer = reinterpret_cast<uint8_t*>(start);
            while (chunk > 0) {
                size_t nread;
                status = read_at(io, buffer, chunk, offset, &nread);
                if (status != ZX_OK) {
                    current_vmar->unmap(start, window);
                    return status;
                }
                buffer += nread;
                offset += nread;
                size -= nread;
                chunk -= nread;
            }
            current_vmar->unmap(start, window);
        }
    }

    *out_vmo = vmo.release();
    *out_size = size;
    return ZX_OK;
}

zx_status_t zxio_vmo_get_copy(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
    zx_status_t status = zxio_vmo_get_clone(io, out_vmo, out_size);
    if (status == ZX_OK) {
        return ZX_OK;
    }

    zx::vmo vmo;
    status = read_file_into_vmo(io, vmo.reset_and_get_address(), out_size);
    if (status != ZX_OK) {
        return status;
    }

    status = vmo.replace(ZX_RIGHTS_BASIC | ZX_RIGHTS_PROPERTY |
                         ZX_RIGHT_READ | ZX_RIGHT_MAP, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    *out_vmo = vmo.release();
    return ZX_OK;
}

zx_status_t zxio_vmo_get_clone(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
    return zxio_vmo_get(io, fuchsia_io_VMO_FLAG_READ |
                            fuchsia_io_VMO_FLAG_EXEC |
                            fuchsia_io_VMO_FLAG_PRIVATE, out_vmo, out_size);
}

zx_status_t zxio_vmo_get_exact(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
    return zxio_vmo_get(io, fuchsia_io_VMO_FLAG_READ |
                            fuchsia_io_VMO_FLAG_EXEC |
                            fuchsia_io_VMO_FLAG_EXACT, out_vmo, out_size);
}

zx_status_t zxio_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo, size_t* out_size) {
    zxio_internal_t* zio = (zxio_internal_t*)io;
    return zio->ops->vmo_get(io, flags, out_vmo, out_size);
}

zx_status_t zxio_open(zxio_t* directory, uint32_t flags, uint32_t mode,
                      const char* path, zxio_t** out_io) {
    zxio_internal_t* zio = (zxio_internal_t*)directory;
    return zio->ops->open(directory, flags, mode, path, out_io);
}

zx_status_t zxio_open_async(zxio_t* directory, uint32_t flags,
                            uint32_t mode, const char* path,
                            zx_handle_t request) {
    zxio_internal_t* zio = (zxio_internal_t*)directory;
    return zio->ops->open_async(directory, flags, mode, path, request);
}

zx_status_t zxio_unlink(zxio_t* directory, const char* path) {
    zxio_internal_t* zio = (zxio_internal_t*)directory;
    return zio->ops->unlink(directory, path);
}

zx_status_t zxio_rename(zxio_t* old_directory, const char* old_path,
                        zxio_t* new_directory, const char* new_path) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_link(zxio_t* src_directory, const char* src_path,
                      zxio_t* dst_directory, const char* dst_path) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_dirent_iterator_init(zxio_dirent_iterator_t* iterator,
                                      zxio_t* directory, void* buffer,
                                      size_t capacity) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                      zxio_dirent_t** out_entry) {
    return ZX_ERR_NOT_SUPPORTED;
}
