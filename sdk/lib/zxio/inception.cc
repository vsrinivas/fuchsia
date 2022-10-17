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
  if (const zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &handle_info,
                                                 sizeof(handle_info), nullptr, nullptr);
      status != ZX_OK) {
    return status;
  }
  zxio_storage_t* storage = nullptr;
  zxio_object_type_t type = ZXIO_OBJECT_TYPE_NONE;
  switch (handle_info.type) {
    case ZX_OBJ_TYPE_CHANNEL: {
      fidl::Arena alloc;
      fidl::ClientEnd<fio::Node> node(zx::channel(std::move(handle)));
      zx::status node_info = zxio_get_nodeinfo(alloc, node);
      if (node_info.is_error()) {
        return node_info.status_value();
      }
      return zxio_create_with_allocator(std::move(node), node_info.value(), allocator, out_context);
    }
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
  if (const zx_status_t status = allocator(type, &storage, out_context);
      status != ZX_OK || storage == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  return zxio_create_with_info(handle.release(), &handle_info, storage);
}

zx_status_t zxio_create_with_allocator(fidl::ClientEnd<fuchsia_io::Node> node,
                                       fuchsia_io::wire::NodeInfoDeprecated& info,
                                       zxio_storage_alloc allocator, void** out_context) {
  zxio_storage_t* storage = nullptr;
  zxio_object_type_t type = ZXIO_OBJECT_TYPE_NONE;
  switch (info.Which()) {
    case fio::wire::NodeInfoDeprecated::Tag::kDatagramSocket:
      type = ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET;
      break;
    case fio::wire::NodeInfoDeprecated::Tag::kDirectory:
      type = ZXIO_OBJECT_TYPE_DIR;
      break;
    case fio::wire::NodeInfoDeprecated::Tag::kFile:
      type = ZXIO_OBJECT_TYPE_FILE;
      break;
    case fio::wire::NodeInfoDeprecated::Tag::kPacketSocket:
      type = ZXIO_OBJECT_TYPE_PACKET_SOCKET;
      break;
    case fio::wire::NodeInfoDeprecated::Tag::kRawSocket:
      type = ZXIO_OBJECT_TYPE_RAW_SOCKET;
      break;
    case fio::wire::NodeInfoDeprecated::Tag::kService:
      type = ZXIO_OBJECT_TYPE_SERVICE;
      break;
    case fio::wire::NodeInfoDeprecated::Tag::kStreamSocket:
      type = ZXIO_OBJECT_TYPE_STREAM_SOCKET;
      break;
    case fio::wire::NodeInfoDeprecated::Tag::kSynchronousDatagramSocket:
      type = ZXIO_OBJECT_TYPE_SYNCHRONOUS_DATAGRAM_SOCKET;
      break;
    case fio::wire::NodeInfoDeprecated::Tag::kTty:
      type = ZXIO_OBJECT_TYPE_TTY;
      break;
  }
  zx_status_t status = allocator(type, &storage, out_context);
  if (status != ZX_OK || storage == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  return zxio_create_with_nodeinfo(std::move(node), info, storage);
}
