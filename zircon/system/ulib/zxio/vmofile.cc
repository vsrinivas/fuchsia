// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

namespace fio = ::llcpp::fuchsia::io;

static zx_status_t zxio_vmofile_close(zxio_t* io) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);
  file->~zxio_vmofile_t();
  return ZX_OK;
}

static zx_status_t zxio_vmofile_release(zxio_t* io, zx_handle_t* out_handle) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);

  sync_mutex_lock(&file->vmo.lock);
  uint64_t seek = file->vmo.offset;
  sync_mutex_unlock(&file->vmo.lock);

  auto result = file->control.Seek(seek, fio::SeekOrigin::START);
  if (result.status() != ZX_OK) {
    return ZX_ERR_BAD_STATE;
  }
  if (result->s != ZX_OK) {
    return ZX_ERR_BAD_STATE;
  }

  *out_handle = file->control.mutable_channel()->release();
  file->~zxio_vmofile_t();
  return ZX_OK;
}

static zx_status_t zxio_vmofile_clone(zxio_t* io, zx_handle_t* out_handle) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  auto result = file->control.Clone(fio::CLONE_FLAG_SAME_RIGHTS, std::move(remote));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  *out_handle = local.release();
  return ZX_OK;
}

static zx_status_t zxio_vmofile_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);
  *out_attr = {};
  out_attr->mode = S_IFREG | S_IRUSR;
  out_attr->content_size = file->vmo.size;
  return ZX_OK;
}

static zx_status_t zxio_vmofile_read(zxio_t* io, void* buffer, size_t capacity,
                                     size_t* out_actual) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);

  sync_mutex_lock(&file->vmo.lock);
  if (capacity > file->vmo.size - file->vmo.offset) {
    capacity = file->vmo.size - file->vmo.offset;
  }
  zx_off_t offset = file->vmo.offset;
  file->vmo.offset += capacity;
  sync_mutex_unlock(&file->vmo.lock);

  zx_status_t status = file->vmo.vmo.read(buffer, file->start + offset, capacity);
  if (status == ZX_OK) {
    *out_actual = capacity;
  }
  return status;
}

static zx_status_t zxio_vmofile_read_at(zxio_t* io, size_t offset, void* buffer, size_t capacity,
                                        size_t* out_actual) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);

  // Make sure we're within the file's bounds.
  if (offset > file->vmo.size) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Clip length to file bounds.
  if (capacity > file->vmo.size - offset) {
    capacity = file->vmo.size - offset;
  }

  zx_status_t status = file->vmo.vmo.read(buffer, file->start + offset, capacity);
  if (status == ZX_OK) {
    *out_actual = capacity;
  }
  return status;
}

static zx_status_t zxio_vmofile_seek(zxio_t* io, size_t offset, zxio_seek_origin_t start,
                                     size_t* out_offset) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);

  sync_mutex_lock(&file->vmo.lock);
  zx_off_t at;
  switch (start) {
    case fio::SeekOrigin::START:
      at = offset;
      break;
    case fio::SeekOrigin::CURRENT:
      at = file->vmo.offset + offset;
      break;
    case fio::SeekOrigin::END:
      at = file->vmo.size + offset;
      break;
    default:
      sync_mutex_unlock(&file->vmo.lock);
      return ZX_ERR_INVALID_ARGS;
  }
  if (at > file->vmo.size) {
    sync_mutex_unlock(&file->vmo.lock);
    return ZX_ERR_OUT_OF_RANGE;
  }
  file->vmo.offset = at;
  sync_mutex_unlock(&file->vmo.lock);

  *out_offset = at;
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_vmofile_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_vmofile_close;
  ops.release = zxio_vmofile_release;
  ops.clone = zxio_vmofile_clone;
  ops.attr_get = zxio_vmofile_attr_get;
  ops.read = zxio_vmofile_read;
  ops.read_at = zxio_vmofile_read_at;
  ops.seek = zxio_vmofile_seek;
  return ops;
}();

zx_status_t zxio_vmofile_init(zxio_storage_t* storage, fio::File::SyncClient control, zx::vmo vmo,
                              zx_off_t offset, zx_off_t length, zx_off_t seek) {
  auto file = new (storage) zxio_vmofile_t{
      .vmo =
          {
              .io = storage->io,
              .vmo = std::move(vmo),
              .size = length,
              .offset = std::min(seek, length),
              .lock = {},
          },
      .start = offset,
      .control = std::move(control),
  };
  zxio_init(&file->vmo.io, &zxio_vmofile_ops);
  return ZX_OK;
}
