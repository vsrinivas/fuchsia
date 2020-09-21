// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_NULL_H_
#define LIB_ZXIO_NULL_H_

#include <lib/zxio/ops.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// Default ---------------------------------------------------------------------

// Default implementations of the ZXIO operations.
//
// These default implementations generally do nothing an return an error. They
// return |ZX_ERR_WRONG_TYPE| or I/O operations (e.g., read, read_at, write,
// write_at, seek) and |ZX_ERR_NOT_SUPPORTED| for other operations.
//
// * |zxio_default_close| does succeed, but does nothing.
// * |zxio_default_wait_begin| returns an invalid handle and no signals.
// * |zxio_default_wait_end| returns no signals.

zx_status_t zxio_default_release(zxio_t* io, zx_handle_t* out_handle);
zx_status_t zxio_default_close(zxio_t* io);
zx_status_t zxio_default_clone(zxio_t* io, zx_handle_t* out_handle);
void zxio_default_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                             zx_signals_t* out_zx_signals);
void zxio_default_wait_end(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals);
zx_status_t zxio_default_sync(zxio_t* io);
zx_status_t zxio_default_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr);
zx_status_t zxio_default_attr_set(zxio_t* io, const zxio_node_attributes_t* attr);
zx_status_t zxio_default_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                               zxio_flags_t flags, size_t* out_actual);
zx_status_t zxio_default_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                  size_t vector_count, zxio_flags_t flags, size_t* out_actual);
zx_status_t zxio_default_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                zxio_flags_t flags, size_t* out_actual);
zx_status_t zxio_default_writev_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                   size_t vector_count, zxio_flags_t flags, size_t* out_actual);
zx_status_t zxio_default_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset,
                              size_t* out_offset);
zx_status_t zxio_default_truncate(zxio_t* io, size_t length);
zx_status_t zxio_default_flags_get(zxio_t* io, uint32_t* out_flags);
zx_status_t zxio_default_flags_set(zxio_t* io, uint32_t flags);
zx_status_t zxio_default_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo,
                                 size_t* out_size);
zx_status_t zxio_default_open(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                              zxio_t** out_io);
zx_status_t zxio_default_open_async(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                                    size_t path_len, zx_handle_t request);
zx_status_t zxio_default_unlink(zxio_t* io, const char* path);
zx_status_t zxio_default_token_get(zxio_t* io, zx_handle_t* out_token);
zx_status_t zxio_default_rename(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                                const char* dst_path);
zx_status_t zxio_default_link(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                              const char* dst_path);
zx_status_t zxio_default_dirent_iterator_init(zxio_t* directory, zxio_dirent_iterator_t* iterator);
zx_status_t zxio_default_dirent_iterator_next(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                              zxio_dirent_t** out_entry);
void zxio_default_dirent_iterator_destroy(zxio_t* io, zxio_dirent_iterator_t* iterator);
zx_status_t zxio_default_isatty(zxio_t* io, bool* tty);

// An ops table filled with the default implementations.
//
// This ops table is a good starting point for building other ops tables to that
// the default implementations of unimplemented operations is consistent across
// ops tables.
static __CONSTEXPR const zxio_ops_t zxio_default_ops = {
    .close = zxio_default_close,
    .release = zxio_default_release,
    .clone = zxio_default_clone,
    .wait_begin = zxio_default_wait_begin,
    .wait_end = zxio_default_wait_end,
    .sync = zxio_default_sync,
    .attr_get = zxio_default_attr_get,
    .attr_set = zxio_default_attr_set,
    .readv = zxio_default_readv,
    .readv_at = zxio_default_readv_at,
    .writev = zxio_default_writev,
    .writev_at = zxio_default_writev_at,
    .seek = zxio_default_seek,
    .truncate = zxio_default_truncate,
    .flags_get = zxio_default_flags_get,
    .flags_set = zxio_default_flags_set,
    .vmo_get = zxio_default_vmo_get,
    .open = zxio_default_open,
    .open_async = zxio_default_open_async,
    .unlink = zxio_default_unlink,
    .token_get = zxio_default_token_get,
    .rename = zxio_default_rename,
    .link = zxio_default_link,
    .dirent_iterator_init = zxio_default_dirent_iterator_init,
    .dirent_iterator_next = zxio_default_dirent_iterator_next,
    .dirent_iterator_destroy = zxio_default_dirent_iterator_destroy,
    .isatty = zxio_default_isatty,
};

// Null ------------------------------------------------------------------------

// Null implementations of the ZXIO operations.
//
// These default implementations correspond to how a null I/O object (e.g., what
// you might get from /dev/null) behaves.
//
// The null implementation is similar to the default implementation, except the
// read, write, and close operations succeed with null effects.

// Initializes a |zxio_t| object with a null ops table.
zx_status_t zxio_null_init(zxio_t* io);

__END_CDECLS

#endif  // LIB_ZXIO_NULL_H_
