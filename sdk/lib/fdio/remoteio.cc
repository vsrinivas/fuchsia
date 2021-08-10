// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <lib/zxio/cpp/inception.h>
#include <poll.h>

#include <fbl/auto_lock.h>

#include "fdio_unistd.h"
#include "private-socket.h"
#include "zxio.h"

namespace fio = fuchsia_io;
namespace fsocket = fuchsia_posix_socket;
namespace frawsocket = fuchsia_posix_socket_raw;

static_assert(FDIO_CHUNK_SIZE >= PATH_MAX, "FDIO_CHUNK_SIZE must be large enough to contain paths");

static_assert(fio::wire::kVmoFlagRead == ZX_VM_PERM_READ, "Vmar / Vmo flags should be aligned");
static_assert(fio::wire::kVmoFlagWrite == ZX_VM_PERM_WRITE, "Vmar / Vmo flags should be aligned");
static_assert(fio::wire::kVmoFlagExec == ZX_VM_PERM_EXECUTE, "Vmar / Vmo flags should be aligned");

zx_status_t fdio_validate_path(const char* path, size_t* out_length) {
  if (path == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  size_t length = strnlen(path, PATH_MAX);
  if (length >= PATH_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (out_length != nullptr) {
    *out_length = length;
  }
  return ZX_OK;
}

// Allocates an fdio_t instance containing storage for a zxio_t object.
static zx_status_t ZxioAllocator(zxio_object_type_t type, zxio_storage_t** out_storage,
                                 void** out_context) {
  fdio_ptr io;
  // The type of storage (fdio subclass) depends on the type of the object until
  // https://fxbug.dev/43267 is resolved, so this has to switch on the type.
  switch (type) {
    case ZXIO_OBJECT_TYPE_DEBUGLOG:
      io = fbl::MakeRefCounted<fdio_internal::zxio>();
      break;
    case ZXIO_OBJECT_TYPE_DEVICE:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    case ZXIO_OBJECT_TYPE_DIR:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    case ZXIO_OBJECT_TYPE_FILE:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    case ZXIO_OBJECT_TYPE_PIPE:
      io = fbl::MakeRefCounted<fdio_internal::zxio>();
      break;
    case ZXIO_OBJECT_TYPE_SERVICE:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    case ZXIO_OBJECT_TYPE_TTY:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    case ZXIO_OBJECT_TYPE_VMO:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    case ZXIO_OBJECT_TYPE_VMOFILE:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    default:
      // Unknown type - allocate a generic fdio object so that zxio_create can
      // initialize a zxio object holding the object for us.
      io = fbl::MakeRefCounted<fdio_internal::zxio>();
      break;
  }
  if (io == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  *out_storage = &io->zxio_storage();
  *out_context = fbl::ExportToRawPtr(&io);
  return ZX_OK;
}

zx::status<fdio_ptr> fdio::create(fidl::ClientEnd<fio::Node> node, fio::wire::NodeInfo info) {
  void* context = nullptr;
  zx_status_t status = zxio_create_with_allocator(std::move(node), info, ZxioAllocator, &context);
  // If the status is ZX_ERR_NO_MEMORY, then zxio_create_with_allocator has not allocated
  // anything and we can return immediately with no cleanup.
  if (status == ZX_ERR_NO_MEMORY) {
    ZX_ASSERT(context == nullptr);
    return zx::error(status);
  }
  // Otherwise, ZxioAllocator has allocated an fdio instance that we now own.
  fdio_ptr io = fbl::ImportFromRawPtr(static_cast<fdio*>(context));
  switch (status) {
    case ZX_OK: {
      return zx::ok(std::move(io));
    }
    case ZX_ERR_NOT_SUPPORTED: {
      zx::handle retrieved_handle;
      status = io->unwrap(retrieved_handle.reset_and_get_address());
      if (status != ZX_OK) {
        return zx::error(status);
      }
      node = fidl::ClientEnd<fio::Node>(zx::channel(std::move(retrieved_handle)));
      break;
    }
    default: {
      return zx::error(status);
    }
  }

  switch (info.which()) {
    case fio::wire::NodeInfo::Tag::kDatagramSocket: {
      auto& socket = info.mutable_datagram_socket();
      return fdio_datagram_socket_create(
          std::move(socket.event), fidl::ClientEnd<fsocket::DatagramSocket>(node.TakeChannel()));
    }
    case fio::wire::NodeInfo::Tag::kStreamSocket: {
      auto& socket = info.mutable_stream_socket().socket;
      return fdio_stream_socket_create(std::move(socket),
                                       fidl::ClientEnd<fsocket::StreamSocket>(node.TakeChannel()));
    }
    case fio::wire::NodeInfo::Tag::kRawSocket: {
      auto& socket = info.mutable_raw_socket();
      return fdio_raw_socket_create(std::move(socket.event),
                                    fidl::ClientEnd<frawsocket::Socket>(node.TakeChannel()));
    }
    default:
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

zx::status<fdio_ptr> fdio::create_with_describe(fidl::ClientEnd<fio::Node> node) {
  auto response = fidl::WireCall(node).Describe();
  zx_status_t status = response.status();
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return fdio::create(std::move(node), std::move(response.value().info));
}

zx::status<fdio_ptr> fdio::create_with_on_open(fidl::ClientEnd<fio::Node> node) {
  class EventHandler : public fidl::WireSyncEventHandler<fio::Node> {
   public:
    explicit EventHandler(fidl::ClientEnd<fio::Node> client_end)
        : client_end_(std::move(client_end)) {}

    zx::status<fdio_ptr>& result() { return result_; };

    const fidl::ClientEnd<fio::Node>& client_end() const { return client_end_; }

    void OnOpen(fidl::WireResponse<fio::Node::OnOpen>* event) override {
      if (event->s != ZX_OK) {
        result_ = zx::error(event->s);
      } else {
        result_ = fdio::create(std::move(client_end_), std::move(event->info));
      }
    }

    zx_status_t Unknown() override { return ZX_ERR_IO; }

   private:
    fidl::ClientEnd<fio::Node> client_end_;
    zx::status<fdio_ptr> result_ = zx::error(ZX_ERR_INTERNAL);
  };

  EventHandler event_handler(std::move(node));
  zx_status_t status = event_handler.HandleOneEvent(event_handler.client_end()).status();
  if (status != ZX_OK) {
    if (status == ZX_ERR_NOT_SUPPORTED) {
      status = ZX_ERR_IO;
    }
    return zx::error(status);
  }
  return event_handler.result();
}

zx::status<fdio_ptr> fdio::create(zx::handle handle) {
  zx_info_handle_basic_t info = {};
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  if (info.type == ZX_OBJ_TYPE_CHANNEL) {
    return fdio::create_with_describe(fidl::ClientEnd<fio::Node>(zx::channel(std::move(handle))));
  }
  // zxio understands how to wrap all non-channel types.
  void* context = nullptr;
  status = zxio_create_with_allocator(std::move(handle), info, ZxioAllocator, &context);
  if (status == ZX_ERR_NO_MEMORY) {
    // If zxio_create_with_allocator returns ZX_ERR_NO_MEMORY, it has not
    // allocated any object and we do not have any cleanup to do.
    ZX_ASSERT(context == nullptr);
    return zx::error(status);
  }
  fdio_ptr io = fbl::ImportFromRawPtr(static_cast<fdio*>(context));
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(io));
}
