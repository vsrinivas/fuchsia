// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/cpp/inception.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "sdk/lib/zxio/private.h"

namespace fio = fuchsia_io;

zx_status_t zxio_create_with_allocator(zx::handle handle, zxio_storage_alloc allocator,
                                       void** out_context) {
  zx_info_handle_basic_t handle_info = {};
  zx_status_t status =
      handle.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  return zxio_create_with_allocator(std::move(handle), handle_info, allocator, out_context);
}

zx_status_t zxio_create_with_allocator(zx::handle handle, const zx_info_handle_basic_t& handle_info,
                                       zxio_storage_alloc allocator, void** out_context) {
  zxio_storage_t* storage = nullptr;
  zxio_object_type_t type = ZXIO_OBJECT_TYPE_NONE;
  switch (handle_info.type) {
    case ZX_OBJ_TYPE_LOG: {
      type = ZXIO_OBJECT_TYPE_DEBUGLOG;
      break;
    }
    case ZX_OBJ_TYPE_SOCKET: {
      type = ZXIO_OBJECT_TYPE_PIPE;
      break;
    }
    case ZX_OBJ_TYPE_VMO: {
      type = ZXIO_OBJECT_TYPE_VMO;
      break;
    }
  }
  zx_status_t status = allocator(type, &storage, out_context);
  if (status != ZX_OK || storage == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  return zxio_create_with_info(handle.release(), &handle_info, storage);
}

zx_status_t zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node> node,
                                       fuchsia_io::wire::NodeInfo& info,
                                       zxio_storage_alloc allocator, void** out_context) {
  zxio_storage_t* storage = nullptr;
  zxio_object_type_t type = ZXIO_OBJECT_TYPE_NONE;
  switch (info.Which()) {
    case fio::wire::NodeInfo::Tag::kDevice:
      type = ZXIO_OBJECT_TYPE_DEVICE;
      break;
    case fio::wire::NodeInfo::Tag::kDirectory:
      type = ZXIO_OBJECT_TYPE_DIR;
      break;
    case fio::wire::NodeInfo::Tag::kFile:
      type = ZXIO_OBJECT_TYPE_FILE;
      break;
    case fio::wire::NodeInfo::Tag::kPipe:
      type = ZXIO_OBJECT_TYPE_PIPE;
      break;
    case fio::wire::NodeInfo::Tag::kService:
      type = ZXIO_OBJECT_TYPE_SERVICE;
      break;
    case fio::wire::NodeInfo::Tag::kTty:
      type = ZXIO_OBJECT_TYPE_TTY;
      break;
    case fio::wire::NodeInfo::Tag::kVmofile:
      type = ZXIO_OBJECT_TYPE_VMOFILE;
      break;
    default:
      break;
  }
  zx_status_t status = allocator(type, &storage, out_context);
  if (status != ZX_OK || storage == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  return zxio_create_with_nodeinfo(std::move(node), info, storage);
}
