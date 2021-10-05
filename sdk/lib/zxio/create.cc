// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket/cpp/wire.h>
#include <lib/fit/defer.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/zxio.h>
#include <stdarg.h>
#include <zircon/syscalls.h>

#include "sdk/lib/zxio/private.h"

namespace fio = fuchsia_io;

namespace {

// A zxio_handle_holder is a zxio object instance that holds on to a handle and
// allows it to be closed or released via zxio_close() / zxio_release(). It is
// useful for wrapping objects that zxio does not understand.
struct zxio_handle_holder {
  zxio_t io;
  zx::handle handle;
};

static_assert(sizeof(zxio_handle_holder) <= sizeof(zxio_storage_t),
              "zxio_handle_holder must fit inside zxio_storage_t.");

zxio_handle_holder& zxio_get_handle_holder(zxio_t* io) {
  return *reinterpret_cast<zxio_handle_holder*>(io);
}

static constexpr zxio_ops_t zxio_handle_holder_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    zxio_get_handle_holder(io).~zxio_handle_holder();
    return ZX_OK;
  };

  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    zx_handle_t handle = zxio_get_handle_holder(io).handle.release();
    if (handle == ZX_HANDLE_INVALID) {
      return ZX_ERR_BAD_HANDLE;
    }
    *out_handle = handle;
    return ZX_OK;
  };
  return ops;
}();

void zxio_handle_holder_init(zxio_storage_t* storage, zx::handle handle) {
  auto holder = new (storage) zxio_handle_holder{
      .handle = std::move(handle),
  };
  zxio_init(&holder->io, &zxio_handle_holder_ops);
}

class ZxioCreateOnOpenEventHandler final : public fidl::WireSyncEventHandler<fio::Node> {
 public:
  ZxioCreateOnOpenEventHandler(fidl::ClientEnd<fio::Node> node, zxio_storage_t* storage,
                               zx_status_t& status)
      : node_(std::move(node)), storage_(storage), status_(status) {}

 protected:
  void OnOpen(fidl::WireResponse<fio::Node::OnOpen>* event) final {
    status_ = event->s;
    if (event->s != ZX_OK)
      return;
    status_ = zxio_create_with_nodeinfo(std::move(node_), event->info, storage_);
  }

  zx_status_t Unknown() final { return ZX_ERR_IO; }

 private:
  fidl::ClientEnd<fio::Node> node_;
  zxio_storage_t* storage_;
  zx_status_t& status_;
};

}  // namespace

zx_status_t zxio_create_with_info(zx_handle_t raw_handle, const zx_info_handle_basic_t* handle_info,
                                  zxio_storage_t* storage) {
  zx::handle handle(raw_handle);
  if (!handle.is_valid() || storage == nullptr || handle_info == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  switch (handle_info->type) {
    case ZX_OBJ_TYPE_CHANNEL: {
      fidl::ClientEnd<fio::Node> node(zx::channel(std::move(handle)));
      fidl::WireResult result = fidl::WireCall(node)->Describe();
      zx_status_t status = result.status();
      if (status != ZX_OK) {
        return status;
      }
      auto node_info = std::move(result.value().info);
      return zxio_create_with_nodeinfo(std::move(node), node_info, storage);
    }
    case ZX_OBJ_TYPE_LOG: {
      zxio_debuglog_init(storage, zx::debuglog(std::move(handle)));
      return ZX_OK;
    }
    case ZX_OBJ_TYPE_SOCKET: {
      zx::socket socket(std::move(handle));
      zx_info_socket_t info;
      zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
      if (status != ZX_OK) {
        return status;
      }
      return zxio_pipe_init(storage, std::move(socket), info);
    }
    case ZX_OBJ_TYPE_VMO: {
      zx::vmo vmo(std::move(handle));
      zx::stream stream;
      uint32_t options = 0u;
      if (handle_info->rights & ZX_RIGHT_READ) {
        options |= ZX_STREAM_MODE_READ;
      }
      if (handle_info->rights & ZX_RIGHT_WRITE) {
        options |= ZX_STREAM_MODE_WRITE;
      }
      // We pass 0 for the initial seek value because the |handle| we're given does not remember
      // the seek value we had previously.
      zx_status_t status = zx::stream::create(options, vmo, 0u, &stream);
      if (status != ZX_OK) {
        zxio_null_init(&storage->io);
        return status;
      }
      return zxio_vmo_init(storage, std::move(vmo), std::move(stream));
    }
    default: {
      zxio_handle_holder_init(storage, std::move(handle));
      return ZX_ERR_NOT_SUPPORTED;
    }
  }
}

zx_status_t zxio_create(zx_handle_t raw_handle, zxio_storage_t* storage) {
  zx::handle handle(raw_handle);
  if (!handle.is_valid() || storage == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_info_handle_basic_t info = {};
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    zxio_null_init(&storage->io);
    return status;
  }
  return zxio_create_with_info(handle.release(), &info, storage);
}

zx_status_t zxio_create_with_on_open(zx_handle_t raw_handle, zxio_storage_t* storage) {
  auto node = fidl::ClientEnd<fio::Node>(zx::channel(raw_handle));
  if (!node.is_valid() || storage == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  fidl::UnownedClientEnd unowned_node = node.borrow();
  zx_status_t handler_status;
  ZxioCreateOnOpenEventHandler handler(std::move(node), storage, handler_status);
  zx_status_t status = handler.HandleOneEvent(unowned_node).status();
  if (status != ZX_OK) {
    return status;
  }
  return handler_status;
}

zx_status_t zxio_create_with_nodeinfo(fidl::ClientEnd<fio::Node> node, fio::wire::NodeInfo& info,
                                      zxio_storage_t* storage) {
  switch (info.which()) {
    case fio::wire::NodeInfo::Tag::kDevice: {
      auto& device = info.mutable_device();
      zx::eventpair event = std::move(device.event);
      return zxio_remote_init(storage, node.TakeChannel().release(), event.release());
    }
    case fio::wire::NodeInfo::Tag::kDirectory: {
      return zxio_dir_init(storage, node.TakeChannel().release());
    }
    case fio::wire::NodeInfo::Tag::kFile: {
      auto& file = info.mutable_file();
      zx::event event = std::move(file.event);
      zx::stream stream = std::move(file.stream);
      return zxio_file_init(storage, node.TakeChannel().release(), event.release(),
                            stream.release());
    }
    case fio::wire::NodeInfo::Tag::kPipe: {
      auto& pipe = info.mutable_pipe();
      zx::socket socket = std::move(pipe.socket);
      zx_info_socket_t socket_info;
      zx_status_t status =
          socket.get_info(ZX_INFO_SOCKET, &socket_info, sizeof(socket_info), nullptr, nullptr);
      if (status != ZX_OK) {
        return status;
      }
      return zxio_pipe_init(storage, std::move(socket), socket_info);
    }
    case fio::wire::NodeInfo::Tag::kService: {
      return zxio_remote_init(storage, node.TakeChannel().release(), ZX_HANDLE_INVALID);
    }
    case fio::wire::NodeInfo::Tag::kTty: {
      auto& tty = info.mutable_tty();
      zx::eventpair event = std::move(tty.event);
      return zxio_remote_init(storage, node.TakeChannel().release(), event.release());
    }
    case fio::wire::NodeInfo::Tag::kVmofile: {
      auto& file = info.mutable_vmofile();
      auto control = fidl::ClientEnd<fio::File>(node.TakeChannel());
      auto result = fidl::WireCall(control.borrow())->Seek(0, fio::wire::SeekOrigin::kStart);
      zx_status_t status = result.status();
      if (status != ZX_OK) {
        return status;
      }
      status = result->s;
      if (status != ZX_OK) {
        return status;
      }
      return zxio_vmofile_init(storage, fidl::BindSyncClient(std::move(control)),
                               std::move(file.vmo), file.offset, file.length, result->offset);
    }
    default: {
      zxio_handle_holder_init(storage, node.TakeChannel());
      return ZX_ERR_NOT_SUPPORTED;
    }
  }
}

zx_status_t zxio_create_with_type(zxio_storage_t* storage, zxio_object_type_t type, ...) {
  va_list args;
  va_start(args, type);
  auto va_cleanup = fit::defer([&args]() { va_end(args); });
  switch (type) {
    case ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET: {
      zx::eventpair event(va_arg(args, zx_handle_t));
      zx::channel client(va_arg(args, zx_handle_t));
      if (!event.is_valid() || !client.is_valid() || storage == nullptr) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_datagram_socket_init(
          storage, std::move(event),
          fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket>(std::move(client)));
    }
    case ZXIO_OBJECT_TYPE_DIR: {
      zx::handle control(va_arg(args, zx_handle_t));
      if (!control.is_valid() || storage == nullptr) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_dir_init(storage, control.release());
    }
    case ZXIO_OBJECT_TYPE_NODE: {
      zx::handle control(va_arg(args, zx_handle_t));
      if (!control.is_valid() || storage == nullptr) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_remote_init(storage, control.release(), ZX_HANDLE_INVALID);
    }
    case ZXIO_OBJECT_TYPE_STREAM_SOCKET: {
      zx::socket socket(va_arg(args, zx_handle_t));
      zx::channel client(va_arg(args, zx_handle_t));
      zx_info_socket_t* info = va_arg(args, zx_info_socket_t*);
      if (!socket.is_valid() || !client.is_valid() || storage == nullptr || info == nullptr) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_stream_socket_init(
          storage, std::move(socket),
          fidl::ClientEnd<fuchsia_posix_socket::StreamSocket>(std::move(client)), *info);
    }
    case ZXIO_OBJECT_TYPE_PIPE: {
      zx::socket socket(va_arg(args, zx_handle_t));
      zx_info_socket_t* info = va_arg(args, zx_info_socket_t*);
      if (!socket.is_valid() || storage == nullptr || info == nullptr) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_pipe_init(storage, std::move(socket), *info);
    }
    case ZXIO_OBJECT_TYPE_RAW_SOCKET: {
      zx::eventpair event(va_arg(args, zx_handle_t));
      zx::channel client(va_arg(args, zx_handle_t));
      if (!event.is_valid() || !client.is_valid() || storage == nullptr) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_raw_socket_init(
          storage, std::move(event),
          fidl::ClientEnd<fuchsia_posix_socket_raw::Socket>(std::move(client)));
    }
    case ZXIO_OBJECT_TYPE_VMO: {
      zx::vmo vmo(va_arg(args, zx_handle_t));
      zx::stream stream(va_arg(args, zx_handle_t));
      if (!vmo.is_valid() || !stream.is_valid() || storage == nullptr) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_vmo_init(storage, std::move(vmo), std::move(stream));
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}
