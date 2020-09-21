// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

#include "private.h"

static_assert(sizeof(zxio_vmo_t) <= sizeof(zxio_storage_t),
              "zxio_vmo_t must fit inside zxio_storage_t.");

static zx_status_t zxio_vmo_close(zxio_t* io) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  file->~zxio_vmo_t();
  return ZX_OK;
}

static zx_status_t zxio_vmo_release(zxio_t* io, zx_handle_t* out_handle) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  *out_handle = file->vmo.release();
  return ZX_OK;
}

static zx_status_t zxio_vmo_clone(zxio_t* io, zx_handle_t* out_handle) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  zx::vmo vmo;
  zx_status_t status = file->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  *out_handle = vmo.release();
  return status;
}

static zx_status_t zxio_vmo_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  *out_attr = {};
  ZXIO_NODE_ATTR_SET(*out_attr, protocols, ZXIO_NODE_PROTOCOL_FILE | ZXIO_NODE_PROTOCOL_MEMORY);
  ZXIO_NODE_ATTR_SET(*out_attr, abilities,
                     ZXIO_OPERATION_READ_BYTES | ZXIO_OPERATION_GET_ATTRIBUTES);
  ZXIO_NODE_ATTR_SET(*out_attr, content_size, file->size);
  return ZX_OK;
}

static zx_status_t zxio_vmo_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                  zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  sync_mutex_lock(&file->lock);
  zx_status_t status =
      zxio_vmo_do_vector(0, file->size, &file->offset, vector, vector_count, out_actual,
                         [&](void* buffer, zx_off_t offset, size_t capacity) {
                           return file->vmo.read(buffer, offset, capacity);
                         });
  sync_mutex_unlock(&file->lock);
  return status;
}

static zx_status_t zxio_vmo_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                     size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  return zxio_vmo_do_vector(0, file->size, &offset, vector, vector_count, out_actual,
                            [&](void* buffer, zx_off_t offset, size_t capacity) {
                              return file->vmo.read(buffer, offset, capacity);
                            });
}

static zx_status_t zxio_vmo_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                   zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  sync_mutex_lock(&file->lock);
  zx_status_t status =
      zxio_vmo_do_vector(0, file->size, &file->offset, vector, vector_count, out_actual,
                         [&](void* buffer, zx_off_t offset, size_t capacity) {
                           return file->vmo.write(buffer, offset, capacity);
                         });
  sync_mutex_unlock(&file->lock);
  return status;
}

static zx_status_t zxio_vmo_writev_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                      size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  return zxio_vmo_do_vector(0, file->size, &offset, vector, vector_count, out_actual,
                            [&](void* buffer, zx_off_t offset, size_t capacity) {
                              return file->vmo.write(buffer, offset, capacity);
                            });
}

zx_status_t zxio_vmo_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset,
                          size_t* out_offset) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  sync_mutex_lock(&file->lock);
  zx_off_t origin;
  switch (start) {
    case ZXIO_SEEK_ORIGIN_START:
      origin = 0;
      break;
    case ZXIO_SEEK_ORIGIN_CURRENT:
      origin = file->offset;
      break;
    case ZXIO_SEEK_ORIGIN_END:
      origin = file->size;
      break;
    default:
      sync_mutex_unlock(&file->lock);
      return ZX_ERR_INVALID_ARGS;
  }
  zx_off_t at;
  if (add_overflow(origin, offset, &at)) {
    sync_mutex_unlock(&file->lock);
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (at > file->size) {
    sync_mutex_unlock(&file->lock);
    return ZX_ERR_OUT_OF_RANGE;
  }
  file->offset = at;
  sync_mutex_unlock(&file->lock);

  *out_offset = at;
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_vmo_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_vmo_close;
  ops.release = zxio_vmo_release;
  ops.clone = zxio_vmo_clone;
  ops.attr_get = zxio_vmo_attr_get;
  ops.readv = zxio_vmo_readv;
  ops.readv_at = zxio_vmo_readv_at;
  ops.writev = zxio_vmo_writev;
  ops.writev_at = zxio_vmo_writev_at;
  ops.seek = zxio_vmo_seek;
  return ops;
}();

zx_status_t zxio_vmo_init(zxio_storage_t* storage, zx::vmo vmo, zx_off_t offset) {
  uint64_t size;
  zx_status_t status = vmo.get_size(&size);
  if (status != ZX_OK) {
    return status;
  }

  auto file = new (storage) zxio_vmo_t{
      .io = storage->io,
      .vmo = std::move(vmo),
      .size = size,
      .offset = std::min(offset, size),
      .lock = {},
  };
  zxio_init(&file->io, &zxio_vmo_ops);
  return ZX_OK;
}
