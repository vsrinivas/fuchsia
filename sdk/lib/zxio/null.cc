// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/null.h>
#include <zircon/syscalls.h>

#include <cerrno>

zx_status_t zxio_default_close(zxio_t* io) { return ZX_OK; }

zx_status_t zxio_default_release(zxio_t* io, zx_handle_t* out_handle) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_borrow(zxio_t* io, zx_handle_t* out_handle) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_clone(zxio_t* io, zx_handle_t* out_handle) { return ZX_ERR_NOT_SUPPORTED; }

void zxio_default_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                             zx_signals_t* out_zx_signals) {
  *out_handle = ZX_HANDLE_INVALID;
  *out_zx_signals = ZX_SIGNAL_NONE;
}

void zxio_default_wait_end(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
  *out_zxio_signals = ZXIO_SIGNAL_NONE;
}

zx_status_t zxio_default_sync(zxio_t* io) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t zxio_default_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                               zxio_flags_t flags, size_t* out_actual) {
  return ZX_ERR_WRONG_TYPE;
}

zx_status_t zxio_default_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                  size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  return ZX_ERR_WRONG_TYPE;
}

zx_status_t zxio_default_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                zxio_flags_t flags, size_t* out_actual) {
  return ZX_ERR_WRONG_TYPE;
}

zx_status_t zxio_default_writev_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                   size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  return ZX_ERR_WRONG_TYPE;
}

zx_status_t zxio_default_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset,
                              size_t* out_offset) {
  return ZX_ERR_WRONG_TYPE;
}

zx_status_t zxio_default_truncate(zxio_t* io, uint64_t length) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t zxio_default_flags_get(zxio_t* io, uint32_t* out_flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t zxio_default_flags_set(zxio_t* io, uint32_t flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t zxio_default_advisory_lock(zxio_t* io, advisory_lock_req* req) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_vmo_get(zxio_t* io, zxio_vmo_flags_t flags, zx_handle_t* out_vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_get_read_buffer_available(zxio_t* io, size_t* out_available) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_shutdown(zxio_t* io, zxio_shutdown_options_t options, int16_t* out_code) {
  *out_code = ENOTSOCK;
  return ZX_OK;
}

zx_status_t zxio_default_open(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                              size_t path_len, zxio_storage_t* storage) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_open_async(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                                    size_t path_len, zx_handle_t request) {
  zx_handle_close(request);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_add_inotify_filter(zxio_t* io, const char* path, size_t path_len,
                                            uint32_t mask, uint32_t watch_descriptor,
                                            zx_handle_t socket) {
  zx_handle_close(socket);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_unlink(zxio_t* io, const char* name, size_t name_len, int flags) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_token_get(zxio_t* io, zx_handle_t* out_token) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_rename(zxio_t* io, const char* old_path, size_t old_path_len,
                                zx_handle_t dst_token, const char* new_path, size_t new_path_len) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_link(zxio_t* io, const char* src_path, size_t src_path_len,
                              zx_handle_t dst_token, const char* dst_path, size_t dst_path_len) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_dirent_iterator_init(zxio_t* directory, zxio_dirent_iterator_t* iterator) {
  iterator->io = directory;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_dirent_iterator_next(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                              zxio_dirent_t* inout_entry) {
  return ZX_ERR_NOT_SUPPORTED;
}

void zxio_default_dirent_iterator_destroy(zxio_t* io, zxio_dirent_iterator_t* iterator) {
  iterator->io = nullptr;
}

zx_status_t zxio_default_isatty(zxio_t* io, bool* tty) {
  *tty = false;
  return ZX_OK;
}

zx_status_t zxio_default_get_window_size(zxio_t* io, uint32_t* width, uint32_t* height) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_set_window_size(zxio_t* io, uint32_t width, uint32_t height) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_watch_directory(zxio_t* io, zxio_watch_directory_cb cb, zx_time_t deadline,
                                         void* context) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_bind(zxio_t* io, const struct sockaddr* addr, socklen_t addrlen,
                              int16_t* out_code) {
  *out_code = ENOTSOCK;
  return ZX_OK;
}

zx_status_t zxio_default_connect(zxio_t* io, const struct sockaddr* addr, socklen_t addrlen,
                                 int16_t* out_code) {
  *out_code = ENOTSOCK;
  return ZX_OK;
}

zx_status_t zxio_default_listen(zxio_t* io, int backlog, int16_t* out_code) {
  *out_code = ENOTSOCK;
  return ZX_OK;
}

zx_status_t zxio_default_accept(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                zxio_storage_t* out_storage, int16_t* out_code) {
  *out_code = ENOTSOCK;
  return ZX_OK;
}

zx_status_t zxio_default_getsockname(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                     int16_t* out_code) {
  *out_code = ENOTSOCK;
  return ZX_OK;
}

zx_status_t zxio_default_getpeername(zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                     int16_t* out_code) {
  *out_code = ENOTSOCK;
  return ZX_OK;
}

zx_status_t zxio_default_getsockopt(zxio_t* io, int level, int optname, void* optval,
                                    socklen_t* optlen, int16_t* out_code) {
  *out_code = ENOTSOCK;
  return ZX_OK;
}

zx_status_t zxio_default_setsockopt(zxio_t* io, int level, int optname, const void* optval,
                                    socklen_t optlen, int16_t* out_code) {
  *out_code = ENOTSOCK;
  return ZX_OK;
}

zx_status_t zxio_default_ioctl(zxio_t* io, int request, int16_t* out_code, va_list va) {
  *out_code = ENOTTY;
  return ZX_OK;
}

zx_status_t zxio_default_init(zxio_t* io) {
  zxio_init(io, &zxio_default_ops);
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_null_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.readv = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                 size_t* out_actual) {
    if (flags) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    return zxio_do_vector(vector, vector_count, out_actual,
                          [](void* buffer, size_t capacity, size_t* out_actual) {
                            *out_actual = 0;
                            return ZX_OK;
                          });
  };
  ops.writev = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                  size_t* out_actual) {
    if (flags) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    return zxio_do_vector(vector, vector_count, out_actual,
                          [](void* buffer, size_t capacity, size_t* out_actual) {
                            *out_actual = capacity;
                            return ZX_OK;
                          });
  };
  return ops;
}();

zx_status_t zxio_null_init(zxio_t* io) {
  zxio_init(io, &zxio_null_ops);
  return ZX_OK;
}
