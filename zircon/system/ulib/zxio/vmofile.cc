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

static zx_status_t zxio_vmofile_destroy(zxio_t* io) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);
  file->~zxio_vmofile_t();
  return ZX_OK;
}

static zx_status_t zxio_vmofile_close(zxio_t* io) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);
  return file->control.Close().status();
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

static zx_status_t zxio_vmofile_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);
  *out_attr = {};
  ZXIO_NODE_ATTR_SET(*out_attr, protocols, ZXIO_NODE_PROTOCOL_FILE | ZXIO_NODE_PROTOCOL_MEMORY);
  ZXIO_NODE_ATTR_SET(*out_attr, abilities,
                     ZXIO_OPERATION_READ_BYTES | ZXIO_OPERATION_GET_ATTRIBUTES);
  ZXIO_NODE_ATTR_SET(*out_attr, content_size, file->vmo.size);
  return ZX_OK;
}

static zx_status_t zxio_vmofile_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                      zxio_flags_t flags, size_t* out_actual) {
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

static zx_status_t zxio_vmofile_readv_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                         size_t vector_count, zxio_flags_t flags,
                                         size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto file = reinterpret_cast<zxio_vmofile_t*>(io);

  return zxio_vmo_do_vector(file->start, file->vmo.size, &offset, vector, vector_count, out_actual,
                            [&](void* buffer, zx_off_t offset, size_t capacity) {
                              return file->vmo.vmo.read(buffer, offset, capacity);
                            });
}

static zx_status_t zxio_vmofile_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo,
                                        size_t* out_size) {
  if (out_vmo == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Can't support Vmofiles with a non-zero start/offset, because we return just
  // a VMO with no other data - like a starting offset - to the user.
  // (Technically we could support any page aligned offset, but that's currently
  // unneeded.)
  auto file = reinterpret_cast<zxio_vmofile_t*>(io);
  if (file->start != 0) {
    return ZX_ERR_NOT_FOUND;
  }

  // Ensure that we return a VMO handle with only the rights requested by the
  // client. For Vmofiles, the server side does not ever see the VMO_FLAG_*
  // options from the client because the VMO is returned in NodeInfo/Vmofile
  // rather than from a File.GetBuffer call.
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY;
  rights |= flags & fio::VMO_FLAG_READ ? ZX_RIGHT_READ : 0;
  rights |= flags & fio::VMO_FLAG_WRITE ? ZX_RIGHT_WRITE : 0;
  rights |= flags & fio::VMO_FLAG_EXEC ? ZX_RIGHT_EXECUTE : 0;

  if (flags & fio::VMO_FLAG_PRIVATE) {
    // Allow SET_PROPERTY only if creating a private child VMO so that the user
    // can set ZX_PROP_NAME (or similar).
    rights |= ZX_RIGHT_SET_PROPERTY;

    uint32_t options = ZX_VMO_CHILD_COPY_ON_WRITE;
    if (flags & fio::VMO_FLAG_EXEC) {
      // Creating a COPY_ON_WRITE child removes ZX_RIGHT_EXECUTE even if the
      // parent VMO has it, and we can't arbitrary add EXECUTE here on the
      // client side. Adding CHILD_NO_WRITE still creates a snapshot and a new
      // VMO object, which e.g. can have a unique ZX_PROP_NAME value, but the
      // returned handle lacks WRITE and maintains EXECUTE.
      if (flags & fio::VMO_FLAG_WRITE) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      options |= ZX_VMO_CHILD_NO_WRITE;
    }

    zx::vmo child_vmo;
    zx_status_t status =
        file->vmo.vmo.create_child(options, file->start, file->vmo.size, &child_vmo);
    if (status != ZX_OK) {
      return status;
    }

    // COPY_ON_WRITE adds ZX_RIGHT_WRITE automatically, but we shouldn't return
    // a handle with that right unless requested using VMO_FLAG_WRITE.
    // TODO(fxbug.dev/36877): Supporting VMO_FLAG_PRIVATE & VMO_FLAG_WRITE for
    // Vmofiles is a bit weird and inconsistent. See bug for more info.
    zx::vmo result;
    status = child_vmo.replace(rights, &result);
    if (status != ZX_OK) {
      return status;
    }
    *out_vmo = result.release();
    if (out_size) {
      *out_size = file->vmo.size;
    }
    return ZX_OK;
  }

  // For !VMO_FLAG_PRIVATE (including VMO_FLAG_EXACT), we just duplicate another
  // handle to the Vmofile's VMO with appropriately scoped rights.
  zx::vmo result;
  zx_status_t status = file->vmo.vmo.duplicate(rights, &result);
  if (status != ZX_OK) {
    return status;
  }
  *out_vmo = result.release();
  if (out_size) {
    *out_size = file->vmo.size;
  }
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_vmofile_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.destroy = zxio_vmofile_destroy;
  ops.close = zxio_vmofile_close;
  ops.release = zxio_vmofile_release;
  ops.clone = zxio_vmofile_clone;
  ops.attr_get = zxio_vmofile_attr_get;
  ops.readv = zxio_vmofile_readv;
  ops.readv_at = zxio_vmofile_readv_at;
  ops.seek = zxio_vmo_seek;
  ops.vmo_get = zxio_vmofile_vmo_get;
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
