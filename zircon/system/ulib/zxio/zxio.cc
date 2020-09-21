// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/extensions.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <string.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <new>
#include <type_traits>

// The private fields of a |zxio_t| object.
//
// In |ops.h|, the |zxio_t| struct is defined as opaque. Clients of the zxio
// library are forbidden from relying upon the structure of |zxio_t| objects.
// To avoid temptation, the details of the structure are defined only in this
// implementation file and are not visible in the header.
typedef struct zxio_internal {
  explicit zxio_internal(const zxio_ops_t* ops)
      : ops(ops), extensions(nullptr), extension_init_func(0) {}

  const zxio_ops_t* ops;

  // See extensions.h
  //
  // Clients may specify |extensions| when creating a |zxio_t| from a channel.
  // When a new |zxio_t| is created from an existing |zxio_t| through
  // opening/cloning, it will inherit the same |extensions| options.
  const zxio_extensions_t* extensions;

  // If applicable, records which function in |extensions| was used to
  // initialize this |zxio_t|.
  uintptr_t extension_init_func;

  uint8_t reserved[7];
} zxio_internal_t;

static_assert(sizeof(zxio_t) == sizeof(zxio_internal_t), "zxio_t should match zxio_internal_t");

// Converters from the public (opaque) types to the internal (implementation) types.
namespace {

zxio_internal_t* to_internal(zxio_t* io) { return reinterpret_cast<zxio_internal_t*>(io); }

const zxio_internal_t* to_internal(const zxio_t* io) {
  return reinterpret_cast<const zxio_internal_t*>(io);
}

}  // namespace

bool zxio_is_valid(const zxio_t* io) {
  const zxio_internal_t* zio = to_internal(io);
  return zio->ops != nullptr;
}

void zxio_init(zxio_t* io, const zxio_ops_t* ops) { new (io) zxio_internal_t(ops); }

const zxio_ops_t* zxio_get_ops(zxio_t* io) {
  const zxio_internal_t* zio = to_internal(io);
  return zio->ops;
}

uintptr_t zxio_extensions_get_init_function(const zxio_t* io) {
  return to_internal(io)->extension_init_func;
}

void zxio_extensions_set(zxio_t* io, const zxio_extensions_t* extensions) {
  to_internal(io)->extensions = extensions;
}

zx_status_t zxio_close(zxio_t* io) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  static_assert(std::is_trivially_destructible<zxio_internal_t>::value,
                "zxio_internal_t must have trivial destructor");
  zxio_internal_t* zio = to_internal(io);
  zx_status_t status = zio->ops->close(io);
  // Poison the object. Double destruction is undefined behavior.
  zio->ops = nullptr;
  return status;
}

zx_status_t zxio_release(zxio_t* io, zx_handle_t* out_handle) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->release(io, out_handle);
}

zx_status_t zxio_clone(zxio_t* io, zx_handle_t* out_handle) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->clone(io, out_handle);
}

zx_status_t zxio_wait_one(zxio_t* io, zxio_signals_t signals, zx_time_t deadline,
                          zxio_signals_t* out_observed) {
  if (!zxio_is_valid(io)) {
    *out_observed = ZXIO_SIGNAL_NONE;
    return ZX_ERR_BAD_HANDLE;
  }
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  zxio_wait_begin(io, signals, &handle, &zx_signals);
  if (handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_signals_t zx_observed = ZX_SIGNAL_NONE;
  zx_status_t status = zx_object_wait_one(handle, zx_signals, deadline, &zx_observed);
  if (status != ZX_OK) {
    return status;
  }
  zxio_wait_end(io, zx_signals, out_observed);
  return ZX_OK;
}

void zxio_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                     zx_signals_t* out_zx_signals) {
  if (!zxio_is_valid(io)) {
    *out_handle = ZX_HANDLE_INVALID;
    *out_zx_signals = ZX_SIGNAL_NONE;
    return;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->wait_begin(io, zxio_signals, out_handle, out_zx_signals);
}

void zxio_wait_end(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
  if (!zxio_is_valid(io)) {
    *out_zxio_signals = ZXIO_SIGNAL_NONE;
    return;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->wait_end(io, zx_signals, out_zxio_signals);
}

zx_status_t zxio_sync(zxio_t* io) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->sync(io);
}

zx_status_t zxio_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->attr_get(io, out_attr);
}

zx_status_t zxio_attr_set(zxio_t* io, const zxio_node_attributes_t* attr) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->attr_set(io, attr);
}

zx_status_t zxio_read(zxio_t* io, void* buffer, size_t capacity, zxio_flags_t flags,
                      size_t* out_actual) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  const zx_iovec_t vector = {
      .buffer = buffer,
      .capacity = capacity,
  };
  return zxio_readv(io, &vector, 1, flags, out_actual);
}

zx_status_t zxio_read_at(zxio_t* io, zx_off_t offset, void* buffer, size_t capacity,
                         zxio_flags_t flags, size_t* out_actual) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  const zx_iovec_t vector = {
      .buffer = buffer,
      .capacity = capacity,
  };
  return zxio_readv_at(io, offset, &vector, 1, flags, out_actual);
}

zx_status_t zxio_write(zxio_t* io, const void* buffer, size_t capacity, zxio_flags_t flags,
                       size_t* out_actual) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  const zx_iovec_t vector = {
      .buffer = const_cast<void*>(buffer),
      .capacity = capacity,
  };
  return zxio_writev(io, &vector, 1, flags, out_actual);
}

zx_status_t zxio_write_at(zxio_t* io, zx_off_t offset, const void* buffer, size_t capacity,
                          zxio_flags_t flags, size_t* out_actual) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  const zx_iovec_t vector = {
      .buffer = const_cast<void*>(buffer),
      .capacity = capacity,
  };
  return zxio_writev_at(io, offset, &vector, 1, flags, out_actual);
}

zx_status_t zxio_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                       zxio_flags_t flags, size_t* out_actual) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->readv(io, vector, vector_count, flags, out_actual);
}

zx_status_t zxio_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                          size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->readv_at(io, offset, vector, vector_count, flags, out_actual);
}

zx_status_t zxio_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                        zxio_flags_t flags, size_t* out_actual) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->writev(io, vector, vector_count, flags, out_actual);
}

zx_status_t zxio_writev_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                           size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->writev_at(io, offset, vector, vector_count, flags, out_actual);
}

static_assert(ZX_STREAM_SEEK_ORIGIN_START == ZXIO_SEEK_ORIGIN_START, "ZXIO should match ZX");
static_assert(ZX_STREAM_SEEK_ORIGIN_CURRENT == ZXIO_SEEK_ORIGIN_CURRENT, "ZXIO should match ZX");
static_assert(ZX_STREAM_SEEK_ORIGIN_END == ZXIO_SEEK_ORIGIN_END, "ZXIO should match ZX");

zx_status_t zxio_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset, size_t* out_offset) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->seek(io, start, offset, out_offset);
}

zx_status_t zxio_truncate(zxio_t* io, size_t length) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->truncate(io, length);
}

zx_status_t zxio_flags_get(zxio_t* io, uint32_t* out_flags) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->flags_get(io, out_flags);
}

zx_status_t zxio_flags_set(zxio_t* io, uint32_t flags) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->flags_set(io, flags);
}

zx_status_t zxio_token_get(zxio_t* io, zx_handle_t* out_token) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->token_get(io, out_token);
}

zx_status_t zxio_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo, size_t* out_size) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->vmo_get(io, flags, out_vmo, out_size);
}

zx_status_t zxio_open(zxio_t* directory, uint32_t flags, uint32_t mode, const char* path,
                      zxio_t** out_io) {
  if (!zxio_is_valid(directory)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(directory);
  return zio->ops->open(directory, flags, mode, path, out_io);
}

zx_status_t zxio_open_async(zxio_t* directory, uint32_t flags, uint32_t mode, const char* path,
                            size_t path_len, zx_handle_t request) {
  if (!zxio_is_valid(directory)) {
    zx_handle_close(request);
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(directory);
  return zio->ops->open_async(directory, flags, mode, path, path_len, request);
}

zx_status_t zxio_unlink(zxio_t* directory, const char* path) {
  if (!zxio_is_valid(directory)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(directory);
  return zio->ops->unlink(directory, path);
}

zx_status_t zxio_rename(zxio_t* old_directory, const char* old_path,
                        zx_handle_t new_directory_token, const char* new_path) {
  if (!zxio_is_valid(old_directory)) {
    zx_handle_close(new_directory_token);
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(old_directory);
  return zio->ops->rename(old_directory, old_path, new_directory_token, new_path);
}

zx_status_t zxio_link(zxio_t* src_directory, const char* src_path, zx_handle_t dst_directory_token,
                      const char* dst_path) {
  if (!zxio_is_valid(src_directory)) {
    zx_handle_close(dst_directory_token);
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(src_directory);
  return zio->ops->link(src_directory, src_path, dst_directory_token, dst_path);
}

zx_status_t zxio_dirent_iterator_init(zxio_dirent_iterator_t* iterator, zxio_t* directory) {
  if (!zxio_is_valid(directory)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(directory);
  return zio->ops->dirent_iterator_init(directory, iterator);
}

zx_status_t zxio_dirent_iterator_next(zxio_dirent_iterator_t* iterator, zxio_dirent_t** out_entry) {
  if (!zxio_is_valid(iterator->io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(iterator->io);
  return zio->ops->dirent_iterator_next(iterator->io, iterator, out_entry);
}

void zxio_dirent_iterator_destroy(zxio_dirent_iterator_t* iterator) {
  if (!zxio_is_valid(iterator->io)) {
    return;
  }
  zxio_internal_t* zio = to_internal(iterator->io);
  zio->ops->dirent_iterator_destroy(iterator->io, iterator);
}

zx_status_t zxio_isatty(zxio_t* io, bool* tty) {
  if (!zxio_is_valid(io)) {
    return ZX_ERR_BAD_HANDLE;
  }
  zxio_internal_t* zio = to_internal(io);
  return zio->ops->isatty(io, tty);
}
