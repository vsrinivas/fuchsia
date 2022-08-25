// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <lib/zxio/cpp/inception.h>
#include <poll.h>

#include <fbl/auto_lock.h>

#include "sdk/lib/fdio/fdio_unistd.h"
#include "sdk/lib/fdio/socket.h"
#include "sdk/lib/fdio/zxio.h"

namespace fio = fuchsia_io;
namespace fsocket = fuchsia_posix_socket;
namespace frawsocket = fuchsia_posix_socket_raw;
namespace fpacketsocket = fuchsia_posix_socket_packet;

static_assert(FDIO_CHUNK_SIZE >= PATH_MAX, "FDIO_CHUNK_SIZE must be large enough to contain paths");

static_assert(static_cast<uint32_t>(fio::wire::VmoFlags::kRead) == ZX_VM_PERM_READ,
              "Vmar / Vmo flags should be aligned");
static_assert(static_cast<uint32_t>(fio::wire::VmoFlags::kWrite) == ZX_VM_PERM_WRITE,
              "Vmar / Vmo flags should be aligned");
static_assert(static_cast<uint32_t>(fio::wire::VmoFlags::kExecute) == ZX_VM_PERM_EXECUTE,
              "Vmar / Vmo flags should be aligned");

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
    case ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET:
      io = fdio_datagram_socket_allocate();
      break;
    case ZXIO_OBJECT_TYPE_DEBUGLOG:
      io = fbl::MakeRefCounted<fdio_internal::zxio>();
      break;
    case ZXIO_OBJECT_TYPE_DIR:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    case ZXIO_OBJECT_TYPE_FILE:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    case ZXIO_OBJECT_TYPE_PACKET_SOCKET:
      io = fdio_packet_socket_allocate();
      break;
    case ZXIO_OBJECT_TYPE_PIPE:
      io = fbl::MakeRefCounted<fdio_internal::pipe>();
      break;
    case ZXIO_OBJECT_TYPE_RAW_SOCKET:
      io = fdio_raw_socket_allocate();
      break;
    case ZXIO_OBJECT_TYPE_SERVICE:
      io = fbl::MakeRefCounted<fdio_internal::remote>();
      break;
    case ZXIO_OBJECT_TYPE_STREAM_SOCKET:
      io = fdio_stream_socket_allocate();
      break;
    case ZXIO_OBJECT_TYPE_SYNCHRONOUS_DATAGRAM_SOCKET:
      io = fdio_synchronous_datagram_socket_allocate();
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
  return zx::make_status(status, std::move(io));
}

zx::status<fdio_ptr> fdio::create_with_on_open(fidl::ClientEnd<fio::Node> node) {
  class EventHandler : public fidl::WireSyncEventHandler<fio::Node> {
   public:
    explicit EventHandler(fidl::ClientEnd<fio::Node> client_end)
        : client_end_(std::move(client_end)) {}

    zx::status<fdio_ptr>& result() { return result_; }

    const fidl::ClientEnd<fio::Node>& client_end() const { return client_end_; }

    void OnOpen(fidl::WireEvent<fio::Node::OnOpen>* event) override {
      if (event->s != ZX_OK) {
        result_ = zx::error(event->s);
      } else {
        result_ = fdio::create(std::move(client_end_), std::move(event->info));
      }
    }

    void OnRepresentation(fidl::WireEvent<fio::Node::OnRepresentation>* event) override {
      result_ = zx::error(ZX_ERR_NOT_SUPPORTED);
    }

   private:
    fidl::ClientEnd<fio::Node> client_end_;
    zx::status<fdio_ptr> result_ = zx::error(ZX_ERR_INTERNAL);
  };

  EventHandler event_handler(std::move(node));
  const fidl::Status status = event_handler.HandleOneEvent(event_handler.client_end());
  if (!status.ok()) {
    // TODO(https://fxbug.dev/30921): This should probably be ZX_ERR_IO (EIO in
    // POSIX) or the transformation to errno should happen differently. This
    // behavior is kept to avoid breaking tests that check for EPIPE when
    // talking to a closed server endpoint.
    if (status.is_peer_closed()) {
      return zx::error(ZX_ERR_PEER_CLOSED);
    }
    return zx::error(ZX_ERR_IO);
  }
  return event_handler.result();
}

zx::status<fdio_ptr> fdio::create(zx::handle handle) {
  zx_info_handle_basic_t info = {};
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  // zxio doesn't yet support all channel types; see fallback list in the other fdio::create
  // overload.
  if (info.type == ZX_OBJ_TYPE_CHANNEL) {
    fidl::ClientEnd<fio::Node> node(zx::channel(std::move(handle)));
    fidl::WireResult result = fidl::WireCall(node)->Describe();
    if (!result.ok()) {
      return zx::error(result.status());
    }
    return fdio::create(std::move(node), std::move(result.value().info));
  }
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
