// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/null.h>
#include <zircon/syscalls.h>

#include "private.h"

zx_status_t zxio_default_close(zxio_t* io) { return ZX_OK; }

zx_status_t zxio_default_release(zxio_t* io, zx_handle_t* out_handle) {
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

zx_status_t zxio_default_truncate(zxio_t* io, size_t length) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t zxio_default_flags_get(zxio_t* io, uint32_t* out_flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t zxio_default_flags_set(zxio_t* io, uint32_t flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t zxio_default_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo,
                                 size_t* out_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_open(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                              zxio_t** out_io) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_open_async(zxio_t* io, uint32_t flags, uint32_t mode, const char* path,
                                    size_t path_len, zx_handle_t request) {
  zx_handle_close(request);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_unlink(zxio_t* io, const char* path) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t zxio_default_token_get(zxio_t* io, zx_handle_t* out_token) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_rename(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                                const char* dst_path) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_link(zxio_t* io, const char* src_path, zx_handle_t dst_token,
                              const char* dst_path) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_dirent_iterator_init(zxio_t* directory, zxio_dirent_iterator_t* iterator) {
  iterator->io = directory;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_default_dirent_iterator_next(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                              zxio_dirent_t** out_entry) {
  return ZX_ERR_NOT_SUPPORTED;
}

void zxio_default_dirent_iterator_destroy(zxio_t* io, zxio_dirent_iterator_t* iterator) {
  iterator->io = nullptr;
}

static zx_status_t zxio_null_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                   zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return zxio_do_vector(vector, vector_count, out_actual,
                        [](void* buffer, size_t capacity, size_t* out_actual) {
                          *out_actual = 0;
                          return ZX_OK;
                        });
}

static zx_status_t zxio_null_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                    zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return zxio_do_vector(vector, vector_count, out_actual,
                        [](void* buffer, size_t capacity, size_t* out_actual) {
                          *out_actual = capacity;
                          return ZX_OK;
                        });
}

zx_status_t zxio_default_isatty(zxio_t* io, bool* tty) {
  *tty = false;
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_null_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.readv = zxio_null_readv;
  ops.writev = zxio_null_writev;
  return ops;
}();

zx_status_t zxio_null_init(zxio_t* io) {
  zxio_init(io, &zxio_null_ops);
  return ZX_OK;
}
