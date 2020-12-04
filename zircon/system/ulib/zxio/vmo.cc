// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

#include <fbl/algorithm.h>

#include "private.h"

typedef struct zxio_vmo {
  // The |zxio_t| control structure for this object.
  zxio_t io;

  // The underlying VMO that stores the data.
  zx::vmo vmo;

  // The stream through which we will read and write the VMO.
  zx::stream stream;
} zxio_vmo_t;

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
  uint64_t content_size = 0u;
  zx_status_t status =
      file->vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
  if (status != ZX_OK) {
    return status;
  }
  *out_attr = {};
  ZXIO_NODE_ATTR_SET(*out_attr, protocols, ZXIO_NODE_PROTOCOL_FILE | ZXIO_NODE_PROTOCOL_MEMORY);
  ZXIO_NODE_ATTR_SET(*out_attr, abilities,
                     ZXIO_OPERATION_READ_BYTES | ZXIO_OPERATION_GET_ATTRIBUTES);
  ZXIO_NODE_ATTR_SET(*out_attr, content_size, content_size);
  return ZX_OK;
}

static zx_status_t zxio_vmo_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                  zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  return file->stream.readv(0, vector, vector_count, out_actual);
}

static zx_status_t zxio_vmo_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                     size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  return file->stream.readv_at(0, offset, vector, vector_count, out_actual);
}

static zx_status_t zxio_vmo_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                   zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  return file->stream.writev(0, vector, vector_count, out_actual);
}

static zx_status_t zxio_vmo_writev_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                      size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  return file->stream.writev_at(0, offset, vector, vector_count, out_actual);
}

static_assert(ZXIO_SEEK_ORIGIN_START == ZX_STREAM_SEEK_ORIGIN_START, "ZXIO should match ZX");
static_assert(ZXIO_SEEK_ORIGIN_CURRENT == ZX_STREAM_SEEK_ORIGIN_CURRENT, "ZXIO should match ZX");
static_assert(ZXIO_SEEK_ORIGIN_END == ZX_STREAM_SEEK_ORIGIN_END, "ZXIO should match ZX");

static zx_status_t zxio_vmo_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset,
                                 size_t* out_offset) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  return file->stream.seek(static_cast<zx_stream_seek_origin_t>(start), offset, out_offset);
}

zx_status_t zxio_vmo_truncate(zxio_t* io, size_t length) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  // TODO(65888): The work done by this function should really be done atomically. There is a
  // similar issue in memfs::VnodeFile::Truncate. It's likely we'll need to add a new syscall to do
  // this operation atomically.

  size_t previous_content_size = 0u;
  zx_status_t status = file->vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &previous_content_size,
                                              sizeof(previous_content_size));
  if (status != ZX_OK) {
    return status;
  }

  status = file->vmo.set_size(length);
  if (status != ZX_OK) {
    return status;
  }

  status = file->vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &length, sizeof(length));
  if (status != ZX_OK) {
    return status;
  }

  if (length < previous_content_size) {
    constexpr size_t kPageSize = static_cast<size_t>(ZX_PAGE_SIZE);
    size_t remaining = kPageSize - (length % kPageSize);
    file->vmo.op_range(ZX_VMO_OP_ZERO, length, remaining, nullptr, 0u);
  }

  return ZX_OK;
}

static zx_status_t zxio_vmo_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo,
                                    size_t* out_size) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);

  size_t content_size = 0u;
  zx_status_t status =
      file->vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
  if (status != ZX_OK) {
    return status;
  }

  return zxio_vmo_get_common(file->vmo, content_size, flags, out_vmo, out_size);
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
  ops.truncate = zxio_vmo_truncate;
  ops.vmo_get = zxio_vmo_vmo_get;
  return ops;
}();

zx_status_t zxio_vmo_init(zxio_storage_t* storage, zx::vmo vmo, zx::stream stream) {
  auto file = new (storage) zxio_vmo_t{
      .io = storage->io,
      .vmo = std::move(vmo),
      .stream = std::move(stream),
  };
  zxio_init(&file->io, &zxio_vmo_ops);
  return ZX_OK;
}
