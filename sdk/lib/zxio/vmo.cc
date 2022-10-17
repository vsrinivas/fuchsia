// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

#include "private.h"

using zxio_vmo_t = struct zxio_vmo {
  // The |zxio_t| control structure for this object.
  zxio_t io;

  // The underlying VMO that stores the data.
  zx::vmo vmo;

  // The stream through which we will read and write the VMO.
  zx::stream stream;
};

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
  ZXIO_NODE_ATTR_SET(*out_attr, protocols, ZXIO_NODE_PROTOCOL_FILE);
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

zx_status_t zxio_vmo_truncate(zxio_t* io, uint64_t length) {
  auto file = reinterpret_cast<zxio_vmo_t*>(io);
  return file->vmo.set_size(length);
}

static zx_status_t zxio_vmo_vmo_get(zxio_t* io, zxio_vmo_flags_t flags, zx_handle_t* out_vmo) {
  if (out_vmo == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  zxio_vmo_t& file = *reinterpret_cast<zxio_vmo_t*>(io);
  zx::vmo& vmo = file.vmo;

  uint64_t size;
  if (zx_status_t status = vmo.get_prop_content_size(&size); status != ZX_OK) {
    return status;
  }

  // Ensure that we return a VMO handle with only the rights requested by the client.

  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY;
  rights |= flags & ZXIO_VMO_READ ? ZX_RIGHT_READ : 0;
  rights |= flags & ZXIO_VMO_WRITE ? ZX_RIGHT_WRITE : 0;
  rights |= flags & ZXIO_VMO_EXECUTE ? ZX_RIGHT_EXECUTE : 0;

  if (flags & ZXIO_VMO_PRIVATE_CLONE) {
    // Allow ZX_RIGHT_SET_PROPERTY only if creating a private child VMO so that the user can set
    // ZX_PROP_NAME (or similar).
    rights |= ZX_RIGHT_SET_PROPERTY;

    uint32_t options = ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE;
    if (flags & ZXIO_VMO_EXECUTE) {
      // Creating a ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE child removes ZX_RIGHT_EXECUTE even if
      // the parent VMO has it, and we can't arbitrarily add ZX_RIGHT_EXECUTE here on the client
      // side. Adding ZX_VMO_CHILD_NO_WRITE still creates a snapshot and a new VMO object, which
      // e.g. can have a unique ZX_PROP_NAME value, but the returned handle lacks ZX_RIGHT_WRITE and
      // maintains ZX_RIGHT_EXECUTE.
      if (flags & ZXIO_VMO_WRITE) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      options |= ZX_VMO_CHILD_NO_WRITE;
    }

    zx::vmo child_vmo;
    zx_status_t status = vmo.create_child(options, 0u, size, &child_vmo);
    if (status != ZX_OK) {
      return status;
    }

    // ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE adds ZX_RIGHT_WRITE automatically, but we shouldn't
    // return a handle with that right unless requested using ZXIO_VMO_WRITE.
    //
    // TODO(fxbug.dev/36877): Supporting ZXIO_VMO_PRIVATE_CLONE & ZXIO_VMO_WRITE for Vmofiles is a
    // bit weird and inconsistent. See bug for more info.
    zx::vmo result;
    status = child_vmo.replace(rights, &result);
    if (status != ZX_OK) {
      return status;
    }
    *out_vmo = result.release();
    return ZX_OK;
  }

  // For !ZXIO_VMO_PRIVATE_CLONE we just duplicate another handle to the Vmofile's VMO with
  // appropriately scoped rights.
  zx::vmo result;
  zx_status_t status = vmo.duplicate(rights, &result);
  if (status != ZX_OK) {
    return status;
  }
  *out_vmo = result.release();
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
