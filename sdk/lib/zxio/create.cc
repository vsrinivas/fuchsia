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

constexpr zxio_ops_t zxio_handle_holder_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    zxio_get_handle_holder(io).~zxio_handle_holder();
    return ZX_OK;
  };

  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    const zx_handle_t handle = zxio_get_handle_holder(io).handle.release();
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
  void OnOpen(fidl::WireEvent<fio::Node::OnOpen>* event) final {
    status_ = [&event = *event, this] {
      if (event.s != ZX_OK) {
        return event.s;
      }
      if (!event.info.has_value()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_create_with_nodeinfo(std::move(node_), event.info.value(), storage_);
    }();
  }

  void OnRepresentation(fidl::WireEvent<fio::Node::OnRepresentation>* event) final {
    status_ = ZX_ERR_NOT_SUPPORTED;
  }

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
      return zxio_with_nodeinfo(std::move(node), [storage](fidl::ClientEnd<fio::Node> node,
                                                           fio::wire::NodeInfoDeprecated& info) {
        return zxio_create_with_nodeinfo(std::move(node), info, storage);
      });
    }
    case ZX_OBJ_TYPE_LOG: {
      zxio_debuglog_init(storage, zx::debuglog(std::move(handle)));
      return ZX_OK;
    }
    case ZX_OBJ_TYPE_SOCKET: {
      zx::socket socket(std::move(handle));
      zx_info_socket_t info;
      const zx_status_t status =
          socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
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
      const zx_status_t status = zx::stream::create(options, vmo, 0u, &stream);
      if (status != ZX_OK) {
        zxio_default_init(&storage->io);
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
  const zx_status_t status =
      handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    zxio_default_init(&storage->io);
    return status;
  }
  return zxio_create_with_info(handle.release(), &info, storage);
}

zx_status_t zxio_create_with_on_open(zx_handle_t raw_handle, zxio_storage_t* storage) {
  fidl::ClientEnd<fio::Node> node{zx::channel(raw_handle)};
  if (!node.is_valid() || storage == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  const fidl::UnownedClientEnd unowned_node = node.borrow();
  zx_status_t handler_status;
  ZxioCreateOnOpenEventHandler handler(std::move(node), storage, handler_status);
  const fidl::Status status = handler.HandleOneEvent(unowned_node);
  if (!status.ok()) {
    if (status.reason() == fidl::Reason::kUnexpectedMessage) {
      return ZX_ERR_IO;
    }
    return status.status();
  }
  return handler_status;
}

zx_status_t zxio_create_with_nodeinfo(fidl::ClientEnd<fio::Node> node,
                                      fio::wire::NodeInfoDeprecated& info,
                                      zxio_storage_t* storage) {
  switch (info.Which()) {
    case fio::wire::NodeInfoDeprecated::Tag::kDatagramSocket: {
      fio::wire::DatagramSocket& datagram_socket = info.datagram_socket();
      zx::socket& socket = datagram_socket.socket;
      zx_info_socket_t info;
      if (const zx_status_t status =
              socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
          status != ZX_OK) {
        return status;
      }
      const zxio_datagram_prelude_size_t prelude_size{
          .tx = datagram_socket.tx_meta_buf_size,
          .rx = datagram_socket.rx_meta_buf_size,
      };
      return zxio_datagram_socket_init(
          storage, std::move(socket), info, prelude_size,
          fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket>(node.TakeChannel()));
    }
    case fio::wire::NodeInfoDeprecated::Tag::kDirectory: {
      return zxio_dir_init(storage, std::move(node));
    }
    case fio::wire::NodeInfoDeprecated::Tag::kFile: {
      fio::wire::FileObject& file = info.file();
      zx::event event = std::move(file.event);
      zx::stream stream = std::move(file.stream);
      return zxio_file_init(storage, std::move(event), std::move(stream), std::move(node));
    }
    case fio::wire::NodeInfoDeprecated::Tag::kPacketSocket: {
      return zxio_packet_socket_init(
          storage, std::move(info.packet_socket().event),
          fidl::ClientEnd<fuchsia_posix_socket_packet::Socket>(node.TakeChannel()));
    }
    case fio::wire::NodeInfoDeprecated::Tag::kRawSocket: {
      return zxio_raw_socket_init(
          storage, std::move(info.raw_socket().event),
          fidl::ClientEnd<fuchsia_posix_socket_raw::Socket>(node.TakeChannel()));
    }
    case fio::wire::NodeInfoDeprecated::Tag::kService: {
      return zxio_remote_init(storage, zx::event{}, std::move(node), /*is_tty=*/false);
    }
    case fio::wire::NodeInfoDeprecated::Tag::kStreamSocket: {
      zx::socket& socket = info.stream_socket().socket;
      zx_info_socket_t info;
      bool is_connected;
      const zx_status_t status =
          socket.wait_one(ZX_USER_SIGNAL_3, zx::time::infinite_past(), nullptr);
      // TODO(tamird): Transferring a listening or connecting socket to another process doesn't work
      // correctly since those states can't be observed here.
      switch (status) {
        case ZX_OK:
          is_connected = true;
          break;
        case ZX_ERR_TIMED_OUT:
          is_connected = false;
          break;
        default:
          return status;
      }
      if (const zx_status_t status =
              socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
          status != ZX_OK) {
        return status;
      }
      return zxio_stream_socket_init(
          storage, std::move(socket), info, is_connected,
          fidl::ClientEnd<fuchsia_posix_socket::StreamSocket>(node.TakeChannel()));
    }
    case fio::wire::NodeInfoDeprecated::Tag::kSynchronousDatagramSocket: {
      return zxio_synchronous_datagram_socket_init(
          storage, std::move(info.synchronous_datagram_socket().event),
          fidl::ClientEnd<fuchsia_posix_socket::SynchronousDatagramSocket>(node.TakeChannel()));
    }
    case fio::wire::NodeInfoDeprecated::Tag::kTty: {
      fio::wire::Tty& tty = info.tty();
      zx::eventpair event = std::move(tty.event);
      return zxio_remote_init(storage, std::move(event), std::move(node), /*is_tty=*/true);
    }
  }
}

zx_status_t zxio_create_with_type(zxio_storage_t* storage, zxio_object_type_t type, ...) {
  va_list args;
  va_start(args, type);
  const fit::deferred_action va_cleanup = fit::defer([&args]() { va_end(args); });
  switch (type) {
    case ZXIO_OBJECT_TYPE_SYNCHRONOUS_DATAGRAM_SOCKET: {
      zx::eventpair event(va_arg(args, zx_handle_t));
      zx::channel client(va_arg(args, zx_handle_t));
      if (storage == nullptr || !event.is_valid() || !client.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_synchronous_datagram_socket_init(
          storage, std::move(event),
          fidl::ClientEnd<fuchsia_posix_socket::SynchronousDatagramSocket>(std::move(client)));
    }
    case ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET: {
      zx::socket socket(va_arg(args, zx_handle_t));
      zx_info_socket_t* info = va_arg(args, zx_info_socket_t*);
      zxio_datagram_prelude_size_t* prelude_size = va_arg(args, zxio_datagram_prelude_size_t*);
      zx::channel client(va_arg(args, zx_handle_t));
      if (storage == nullptr || !socket.is_valid() || info == nullptr || prelude_size == nullptr ||
          !client.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_datagram_socket_init(
          storage, std::move(socket), *info, *prelude_size,
          fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket>(std::move(client)));
    }
    case ZXIO_OBJECT_TYPE_DIR: {
      zx::channel control(va_arg(args, zx_handle_t));
      if (storage == nullptr || !control.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_dir_init(storage, fidl::ClientEnd<fio::Node>{std::move(control)});
    }
    case ZXIO_OBJECT_TYPE_NODE: {
      zx::channel control(va_arg(args, zx_handle_t));
      if (storage == nullptr || !control.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_remote_init(storage, zx::event{}, fidl::ClientEnd<fio::Node>{std::move(control)},
                              /*is_tty=*/false);
    }
    case ZXIO_OBJECT_TYPE_STREAM_SOCKET: {
      zx::socket socket(va_arg(args, zx_handle_t));
      zx_info_socket_t* info = va_arg(args, zx_info_socket_t*);
      const bool is_connected = va_arg(args, int);
      zx::channel client(va_arg(args, zx_handle_t));
      if (storage == nullptr || !socket.is_valid() || info == nullptr || !client.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_stream_socket_init(
          storage, std::move(socket), *info, is_connected,
          fidl::ClientEnd<fuchsia_posix_socket::StreamSocket>(std::move(client)));
    }
    case ZXIO_OBJECT_TYPE_PIPE: {
      zx::socket socket(va_arg(args, zx_handle_t));
      zx_info_socket_t* info = va_arg(args, zx_info_socket_t*);
      if (storage == nullptr || !socket.is_valid() || info == nullptr) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_pipe_init(storage, std::move(socket), *info);
    }
    case ZXIO_OBJECT_TYPE_RAW_SOCKET: {
      zx::eventpair event(va_arg(args, zx_handle_t));
      zx::channel client(va_arg(args, zx_handle_t));
      if (storage == nullptr || !event.is_valid() || !client.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_raw_socket_init(
          storage, std::move(event),
          fidl::ClientEnd<fuchsia_posix_socket_raw::Socket>(std::move(client)));
    }
    case ZXIO_OBJECT_TYPE_PACKET_SOCKET: {
      zx::eventpair event(va_arg(args, zx_handle_t));
      zx::channel client(va_arg(args, zx_handle_t));
      if (storage == nullptr || !event.is_valid() || !client.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_packet_socket_init(
          storage, std::move(event),
          fidl::ClientEnd<fuchsia_posix_socket_packet::Socket>(std::move(client)));
    }
    case ZXIO_OBJECT_TYPE_VMO: {
      zx::vmo vmo(va_arg(args, zx_handle_t));
      zx::stream stream(va_arg(args, zx_handle_t));
      if (storage == nullptr || !vmo.is_valid() || !stream.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
      }
      return zxio_vmo_init(storage, std::move(vmo), std::move(stream));
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}
