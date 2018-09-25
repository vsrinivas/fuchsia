// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>

struct zxio {
    const zxio_ops_t* ops;
    uint64_t reserved[3];
};

static_assert(sizeof(zxio_t) == 4 * sizeof(uint64_t),
              "zxio_ctx_get should return a pointer just after zxio_t");

zx_status_t zxio_alloc(const zxio_ops_t* ops, size_t ctx_size,
                       zxio_t** out_io) {
    zxio_t* io = static_cast<zxio_t*>(calloc(1, sizeof(zxio_t) + ctx_size));
    io->ops = ops;
    *out_io = io;
    return ZX_OK;
}

zx_status_t zxio_acquire_node(zx_handle_t node, zxio_t** out_io) {
    zx_handle_close(node);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_acquire_socket(zx_handle_t socket, zxio_t** out_io) {
    zx_handle_close(socket);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_release(zxio_t* io, zx_handle_t* out_node) {
    zx_status_t status = io->ops->release(zxio_ctx_get(io), out_node);
    free(io);
    return status;
}

zx_status_t zxio_close(zxio_t* io) {
    zx_status_t status = io->ops->close(zxio_ctx_get(io));
    free(io);
    return status;
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
    return io->ops->clone_async(zxio_ctx_get(io), flags, request);
}

zx_status_t zxio_sync(zxio_t* io) {
    return io->ops->sync(zxio_ctx_get(io));
}

zx_status_t zxio_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
    return io->ops->attr_get(zxio_ctx_get(io), out_attr);
}

zx_status_t zxio_attr_set(zxio_t* io, uint32_t flags,
                          const zxio_node_attr_t* attr) {
    return io->ops->attr_set(zxio_ctx_get(io), flags, attr);
}

zx_status_t zxio_read(zxio_t* io, void* buffer, size_t capacity,
                      size_t* out_actual) {
    return io->ops->read(zxio_ctx_get(io), buffer, capacity, out_actual);
}

zx_status_t zxio_read_at(zxio_t* io, size_t offset, void* buffer,
                         size_t capacity, size_t* out_actual) {
    return io->ops->read_at(zxio_ctx_get(io), offset, buffer, capacity,
                              out_actual);
}

zx_status_t zxio_write(zxio_t* io, const void* buffer, size_t capacity,
                       size_t* out_actual) {
    return io->ops->write(zxio_ctx_get(io), buffer, capacity, out_actual);
}

zx_status_t zxio_write_at(zxio_t* io, size_t offset, const void* buffer,
                          size_t capacity, size_t* out_actual) {
    return io->ops->write_at(zxio_ctx_get(io), offset, buffer, capacity,
                             out_actual);
}

zx_status_t zxio_seek(zxio_t* io, size_t offset,
                      zxio_seek_origin_t start, size_t* out_offset) {
    return io->ops->seek(zxio_ctx_get(io), offset, start, out_offset);
}

zx_status_t zxio_trucate(zxio_t* io, size_t length) {
    return io->ops->trucate(zxio_ctx_get(io), length);
}

zx_status_t zxio_flags_get(zxio_t* io, uint32_t* out_flags) {
    return io->ops->flags_get(zxio_ctx_get(io), out_flags);
}

zx_status_t zxio_flags_set(zxio_t* io, uint32_t flags) {
    return io->ops->flags_set(zxio_ctx_get(io), flags);
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
    return directory->ops->open(zxio_ctx_get(directory), flags, mode, path,
                                out_io);
}

zx_status_t zxio_open_async(zxio_t* directory, uint32_t flags,
                            uint32_t mode, const char* path,
                            zx_handle_t request) {
    return directory->ops->open_async(zxio_ctx_get(directory), flags, mode,
                                      path, request);
}

zx_status_t zxio_unlink(zxio_t* directory, const char* path) {
    return directory->ops->unlink(zxio_ctx_get(directory), path);
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
