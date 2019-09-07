// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

namespace fio = ::llcpp::fuchsia::io;

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
  file->~zxio_vmo_t();
  return ZX_OK;
}

static zx_status_t zxio_vmo_clone(zxio_t* io, zx_handle_t* out_handle) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  zx::vmo vmo;
  zx_status_t status = file->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  *out_handle = vmo.release();
  return status;
}

static zx_status_t zxio_vmo_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  *out_attr = {};
  out_attr->mode = S_IFREG | S_IRUSR;
  out_attr->content_size = file->size;
  return ZX_OK;
}

static zx_status_t zxio_vmo_read(zxio_t* io, void* buffer, size_t capacity, size_t* out_actual) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  sync_mutex_lock(&file->lock);
  if (capacity > (file->size - file->offset)) {
    capacity = file->size - file->offset;
  }
  zx_off_t offset = file->offset;
  file->offset += capacity;
  sync_mutex_unlock(&file->lock);

  zx_status_t status = file->vmo.read(buffer, offset, capacity);
  if (status == ZX_OK) {
    *out_actual = capacity;
  }
  return status;
}

static zx_status_t zxio_vmo_read_at(zxio_t* io, size_t offset, void* buffer, size_t capacity,
                                    size_t* out_actual) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  if (offset > file->size) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (capacity > file->size - offset) {
    capacity = file->size - offset;
  }

  zx_status_t status = file->vmo.read(buffer, offset, capacity);
  if (status == ZX_OK) {
    *out_actual = capacity;
  }
  return status;
}

zx_status_t zxio_vmo_write(zxio_t* io, const void* buffer, size_t capacity, size_t* out_actual) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  sync_mutex_lock(&file->lock);
  if (capacity > (file->size - file->offset)) {
    capacity = file->size - file->offset;
  }
  zx_off_t offset = file->offset;
  file->offset += capacity;
  sync_mutex_unlock(&file->lock);

  zx_status_t status = file->vmo.write(buffer, offset, capacity);
  if (status == ZX_OK) {
    *out_actual = capacity;
  }
  return status;
}

zx_status_t zxio_vmo_write_at(zxio_t* io, size_t offset, const void* buffer, size_t capacity,
                              size_t* out_actual) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  if (offset > file->size) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (capacity > file->size - offset) {
    capacity = file->size - offset;
  }

  zx_status_t status = file->vmo.write(buffer, offset, capacity);
  if (status == ZX_OK) {
    *out_actual = capacity;
  }
  return status;
}

static zx_status_t zxio_vmo_seek(zxio_t* io, size_t offset, zxio_seek_origin_t start,
                                 size_t* out_offset) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  sync_mutex_lock(&file->lock);
  zx_off_t at;
  switch (start) {
    case fio::SeekOrigin::START:
      at = offset;
      break;
    case fio::SeekOrigin::CURRENT:
      at = file->offset + offset;
      break;
    case fio::SeekOrigin::END:
      at = file->size + offset;
      break;
    default:
      sync_mutex_unlock(&file->lock);
      return ZX_ERR_INVALID_ARGS;
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
  ops.read = zxio_vmo_read;
  ops.read_at = zxio_vmo_read_at;
  ops.write = zxio_vmo_write;
  ops.write_at = zxio_vmo_write_at;
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
