// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

#include "private.h"

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
  ZXIO_NODE_ATTR_SET(*out_attr, protocols, ZXIO_NODE_PROTOCOL_FILE | ZXIO_NODE_PROTOCOL_MEMORY);
  ZXIO_NODE_ATTR_SET(*out_attr, abilities,
                     ZXIO_OPERATION_READ_BYTES | ZXIO_OPERATION_GET_ATTRIBUTES);
  ZXIO_NODE_ATTR_SET(*out_attr, content_size, file->vmo.size);
  return ZX_OK;
}

static zx_status_t zxio_vmofile_read_vector(zxio_t* io, const zx_iovec_t* vector,
                                            size_t vector_count, zxio_flags_t flags,
                                            size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmofile_t*>(io);

  sync_mutex_lock(&file->vmo.lock);
  zx_status_t status =
      zxio_vmo_do_vector(file->start, file->vmo.size, &file->vmo.offset, vector, vector_count,
                         out_actual, [&](void* buffer, zx_off_t offset, size_t capacity) {
                           return file->vmo.vmo.read(buffer, offset, capacity);
                         });
  sync_mutex_unlock(&file->vmo.lock);
  return status;
}

static zx_status_t zxio_vmofile_read_vector_at(zxio_t* io, zx_off_t offset,
                                               const zx_iovec_t* vector, size_t vector_count,
                                               zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmofile_t*>(io);

  return zxio_vmo_do_vector(file->start, file->vmo.size, &offset, vector, vector_count, out_actual,
                            [&](void* buffer, zx_off_t offset, size_t capacity) {
                              return file->vmo.vmo.read(buffer, offset, capacity);
                            });
}

static constexpr zxio_ops_t zxio_vmofile_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_vmofile_close;
  ops.release = zxio_vmofile_release;
  ops.clone = zxio_vmofile_clone;
  ops.attr_get = zxio_vmofile_attr_get;
  ops.read_vector = zxio_vmofile_read_vector;
  ops.read_vector_at = zxio_vmofile_read_vector_at;
  ops.seek = zxio_vmo_seek;
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
