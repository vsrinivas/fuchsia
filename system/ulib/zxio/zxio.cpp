// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
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

zx_status_t zxio_vmo_get_copy(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_vmo_get_clone(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_vmo_get_exact(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
    return ZX_ERR_NOT_SUPPORTED;
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
