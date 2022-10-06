// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_NULL_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_NULL_H_

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
zx_status_t zxio_default_borrow(zxio_t* io, zx_handle_t* out_handle);
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
zx_status_t zxio_default_truncate(zxio_t* io, uint64_t length);
zx_status_t zxio_default_flags_get(zxio_t* io, uint32_t* out_flags);
zx_status_t zxio_default_flags_set(zxio_t* io, uint32_t flags);
zx_status_t zxio_default_vmo_get(zxio_t* io, zxio_vmo_flags_t flags, zx_handle_t* out_vmo);
zx_status_t zxio_default_get_read_buffer_available(zxio_t* io, size_t* out_available);
zx_status_t zxio_default_shutdown(zxio_t* io, zxio_shutdown_options_t options, int16_t* out_code);
zx_status_t zxio_default_open(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                              size_t path_len, zxio_storage_t* storage);
zx_status_t zxio_default_open_async(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                                    size_t path_len, zx_handle_t request);
zx_status_t zxio_default_add_inotify_filter(zxio_t* io, const char* path, size_t path_len,
                                            uint32_t mask, uint32_t watch_descriptor,
                                            zx_handle_t socket);
zx_status_t zxio_default_unlink(zxio_t* io, const char* name, size_t name_len, int flags);
zx_status_t zxio_default_token_get(zxio_t* io, zx_handle_t* out_token);
zx_status_t zxio_default_rename(zxio_t* io, const char* old_path, size_t old_path_len,
                                zx_handle_t dst_token, const char* new_path, size_t new_path_len);
zx_status_t zxio_default_link(zxio_t* io, const char* src_path, size_t src_path_len,
                              zx_handle_t dst_token, const char* dst_path, size_t dst_path_len);
zx_status_t zxio_default_dirent_iterator_init(zxio_t* directory, zxio_dirent_iterator_t* iterator);
zx_status_t zxio_default_dirent_iterator_next(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                              zxio_dirent_t* inout_entry);
void zxio_default_dirent_iterator_destroy(zxio_t* io, zxio_dirent_iterator_t* iterator);
zx_status_t zxio_default_isatty(zxio_t* io, bool* tty);
zx_status_t zxio_default_get_window_size(zxio_t* io, uint32_t* width, uint32_t* height);
zx_status_t zxio_default_set_window_size(zxio_t* io, uint32_t width, uint32_t height);
zx_status_t zxio_default_advisory_lock(zxio_t* io, struct advisory_lock_req* req);
zx_status_t zxio_default_watch_directory(zxio_t* io, zxio_watch_directory_cb cb, zx_time_t deadline,
                                         void* context);
zx_status_t zxio_default_bind(zxio_t* io, const struct sockaddr* addr, socklen_t addrlen,
                              int16_t* out_code);
zx_status_t zxio_default_connect(zxio_t* io, const struct sockaddr* addr, socklen_t addrlen,
                                 int16_t* out_code);
zx_status_t zxio_default_listen(zxio_t* io, int backlog, int16_t* out_code);
zx_status_t zxio_default_accept(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                zxio_storage_t* out_storage, int16_t* out_code);
zx_status_t zxio_default_getsockname(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                     int16_t* out_code);
zx_status_t zxio_default_getpeername(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                     int16_t* out_code);
zx_status_t zxio_default_getsockopt(zxio_t* io, int level, int optname, void* optval,
                                    socklen_t* optlen, int16_t* out_code);
zx_status_t zxio_default_setsockopt(zxio_t* io, int level, int optname, const void* optval,
                                    socklen_t optlen, int16_t* out_code);
zx_status_t zxio_default_ioctl(zxio_t* io, int request, int16_t* out_code, va_list va);

// An ops table filled with the default implementations.
//
// This ops table is a good starting point for building other ops tables so that
// the default implementations of unimplemented operations is consistent across
// ops tables.
static __CONSTEXPR const zxio_ops_t zxio_default_ops = {
    .close = zxio_default_close,
    .release = zxio_default_release,
    .borrow = zxio_default_borrow,
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
    .get_read_buffer_available = zxio_default_get_read_buffer_available,
    .shutdown = zxio_default_shutdown,
    .open = zxio_default_open,
    .open_async = zxio_default_open_async,
    .add_inotify_filter = zxio_default_add_inotify_filter,
    .unlink = zxio_default_unlink,
    .token_get = zxio_default_token_get,
    .rename = zxio_default_rename,
    .link = zxio_default_link,
    .dirent_iterator_init = zxio_default_dirent_iterator_init,
    .dirent_iterator_next = zxio_default_dirent_iterator_next,
    .dirent_iterator_destroy = zxio_default_dirent_iterator_destroy,
    .isatty = zxio_default_isatty,
    .get_window_size = zxio_default_get_window_size,
    .set_window_size = zxio_default_set_window_size,
    .advisory_lock = zxio_default_advisory_lock,
    .watch_directory = zxio_default_watch_directory,
    .bind = zxio_default_bind,
    .connect = zxio_default_connect,
    .listen = zxio_default_listen,
    .accept = zxio_default_accept,
    .getsockname = zxio_default_getsockname,
    .getpeername = zxio_default_getpeername,
    .getsockopt = zxio_default_getsockopt,
    .setsockopt = zxio_default_setsockopt,
    .ioctl = zxio_default_ioctl,
};

// Default implementations of the ZXIO operations.
zx_status_t zxio_default_init(zxio_t* io);

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

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_NULL_H_
