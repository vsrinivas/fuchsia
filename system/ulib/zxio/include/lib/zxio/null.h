// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_NULL_H_
#define LIB_ZXIO_NULL_H_

#include <lib/zxio/ops.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

zx_status_t zxio_null_release(zxio_t* io, zx_handle_t* out_handle);
zx_status_t zxio_null_close(zxio_t* io);
void zxio_null_wait_begin(zxio_t* io, zxio_signals_t zxio_signals,
                          zx_handle_t* out_handle, zx_signals_t* out_zx_signals);
void zxio_null_wait_end(zxio_t* io, zx_signals_t zx_signals,
                        zxio_signals_t* out_zxio_signals);
zx_status_t zxio_null_clone_async(zxio_t* io, uint32_t flags,
                                  zx_handle_t request);
zx_status_t zxio_null_sync(zxio_t* io);
zx_status_t zxio_null_attr_get(zxio_t* io, zxio_node_attr_t* out_attr);
zx_status_t zxio_null_attr_set(zxio_t* io, uint32_t flags,
                               const zxio_node_attr_t* attr);
zx_status_t zxio_null_read(zxio_t* io, void* buffer, size_t capacity,
                           size_t* out_actual);
zx_status_t zxio_null_read_at(zxio_t* io, size_t offset, void* buffer,
                              size_t capacity, size_t* out_actual);
zx_status_t zxio_null_write(zxio_t* io, const void* buffer, size_t capacity,
                            size_t* out_actual);
zx_status_t zxio_null_write_at(zxio_t* io, size_t offset, const void* buffer,
                               size_t capacity, size_t* out_actual);
zx_status_t zxio_null_seek(zxio_t* io, size_t offset, zxio_seek_origin_t start,
                           size_t* out_offset);
zx_status_t zxio_null_truncate(zxio_t* io, size_t length);
zx_status_t zxio_null_flags_get(zxio_t* io, uint32_t* out_flags);
zx_status_t zxio_null_flags_set(zxio_t* io, uint32_t flags);
zx_status_t zxio_null_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo,
                              size_t* out_size);
zx_status_t zxio_null_open(zxio_t* io, uint32_t flags, uint32_t mode,
                           const char* path, zxio_t** out_io);
zx_status_t zxio_null_open_async(zxio_t* io, uint32_t flags, uint32_t mode,
                                 const char* path, zx_handle_t request);
zx_status_t zxio_null_unlink(zxio_t* io, const char* path);
zx_status_t zxio_null_token_get(zxio_t* io, zx_handle_t* out_token);
zx_status_t zxio_null_rename(zxio_t* io, const char* src_path,
                             zx_handle_t dst_token, const char* dst_path);
zx_status_t zxio_null_link(zxio_t* io, const char* src_path,
                           zx_handle_t dst_token, const char* dst_path);
zx_status_t zxio_null_readdir(zxio_t* io, void* buffer, size_t capacity,
                              size_t* out_actual);
zx_status_t zxio_null_rewind(zxio_t* io);

__CONSTEXPR const zxio_ops_t zxio_null_ops = {
    .release = zxio_null_release,
    .close = zxio_null_close,
    .wait_begin = zxio_null_wait_begin,
    .wait_end = zxio_null_wait_end,
    .clone_async = zxio_null_clone_async,
    .sync = zxio_null_sync,
    .attr_get = zxio_null_attr_get,
    .attr_set = zxio_null_attr_set,
    .read = zxio_null_read,
    .read_at = zxio_null_read_at,
    .write = zxio_null_write,
    .write_at = zxio_null_write_at,
    .seek = zxio_null_seek,
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

__END_CDECLS

#endif // LIB_ZXIO_NULL_H_
