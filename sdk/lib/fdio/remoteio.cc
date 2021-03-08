// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/unsafe.h>
#include <poll.h>
#include <zircon/device/vfs.h>

#include <fbl/auto_lock.h>

#include "lib/zx/channel.h"
#include "private-socket.h"

namespace fio = fuchsia_io;
namespace fpty = fuchsia_hardware_pty;
namespace fsocket = fuchsia_posix_socket;
namespace fdevice = fuchsia_device;

#define ZXDEBUG 0

static_assert(FDIO_CHUNK_SIZE >= PATH_MAX, "FDIO_CHUNK_SIZE must be large enough to contain paths");

static_assert(fio::wire::VMO_FLAG_READ == ZX_VM_PERM_READ, "Vmar / Vmo flags should be aligned");
static_assert(fio::wire::VMO_FLAG_WRITE == ZX_VM_PERM_WRITE, "Vmar / Vmo flags should be aligned");
static_assert(fio::wire::VMO_FLAG_EXEC == ZX_VM_PERM_EXECUTE, "Vmar / Vmo flags should be aligned");

static_assert(fio::wire::DEVICE_SIGNAL_READABLE == fdevice::wire::DEVICE_SIGNAL_READABLE);
static_assert(fio::wire::DEVICE_SIGNAL_OOB == fdevice::wire::DEVICE_SIGNAL_OOB);
static_assert(fio::wire::DEVICE_SIGNAL_WRITABLE == fdevice::wire::DEVICE_SIGNAL_WRITABLE);
static_assert(fio::wire::DEVICE_SIGNAL_ERROR == fdevice::wire::DEVICE_SIGNAL_ERROR);
static_assert(fio::wire::DEVICE_SIGNAL_HANGUP == fdevice::wire::DEVICE_SIGNAL_HANGUP);

// The |mode| argument used for |fuchsia.io.Directory/Open| calls.
#define FDIO_CONNECT_MODE ((uint32_t)0755)

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

__EXPORT
zx_status_t fdio_service_connect(const char* path, zx_handle_t h) {
  return fdio_open(path, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, h);
}

__EXPORT
zx_status_t fdio_service_connect_at(zx_handle_t dir, const char* path, zx_handle_t request_raw) {
  auto request = fidl::ServerEnd<fio::Node>(zx::channel(request_raw));
  auto directory = fidl::UnownedClientEnd<fio::Directory>(dir);
  if (!directory.is_valid()) {
    return ZX_ERR_UNAVAILABLE;
  }

  size_t length = 0u;
  zx_status_t status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return status;
  }
  uint32_t flags = fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_WRITABLE;
  return fio::Directory::Call::Open(directory, flags, FDIO_CONNECT_MODE,
                                    fidl::unowned_str(path, length), std::move(request))
      .status();
}

__EXPORT
zx_status_t fdio_service_connect_by_name(const char name[], zx_handle_t request) {
  static zx::channel service_root;

  {
    static std::once_flag once;
    static zx_status_t status;
    std::call_once(once, [&]() {
      zx::channel request;
      status = zx::channel::create(0, &service_root, &request);
      if (status != ZX_OK) {
        return;
      }
      // TODO(abarth): Use "/svc/" once that actually works.
      status = fdio_service_connect("/svc/.", request.release());
    });
    if (status != ZX_OK) {
      return status;
    }
  }

  return fdio_service_connect_at(service_root.get(), name, request);
}

__EXPORT
zx_status_t fdio_open(const char* path, uint32_t flags, zx_handle_t request) {
  auto handle = zx::handle(request);
  // TODO: fdio_validate_path?
  if (path == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  // Otherwise attempt to connect through the root namespace
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_get_installed(&ns);
  if (status != ZX_OK) {
    return status;
  }
  return fdio_ns_connect(ns, path, flags, handle.release());
}

__EXPORT
zx_status_t fdio_open_at(zx_handle_t dir, const char* path, uint32_t flags,
                         zx_handle_t raw_request) {
  auto request = fidl::ServerEnd<fio::Node>(zx::channel(raw_request));
  auto directory = fidl::UnownedClientEnd<fio::Directory>(dir);
  if (!directory.is_valid()) {
    return ZX_ERR_UNAVAILABLE;
  }

  size_t length;
  zx_status_t status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return status;
  }

  if (flags & ZX_FS_FLAG_DESCRIBE) {
    return ZX_ERR_INVALID_ARGS;
  }

  return fio::Directory::Call::Open(directory, flags, FDIO_CONNECT_MODE,
                                    fidl::unowned_str(path, length), std::move(request))
      .status();
}

static zx_status_t fdio_open_fd_common(fdio_t* iodir, const char* path, uint32_t flags,
                                       int* out_fd) {
  // We're opening a file descriptor rather than just a channel (like fdio_open), so we always want
  // to Describe (or listen for an OnOpen event on) the opened connection. This ensures that the fd
  // is valid before returning from here, and mimics how open() and openat() behave
  // (fdio_flags_to_zxio always add _FLAG_DESCRIBE).
  flags |= ZX_FS_FLAG_DESCRIBE;

  fdio_t* io;
  zx_status_t status = iodir->ops().open(iodir, path, flags, FDIO_CONNECT_MODE, &io);
  if (status != ZX_OK) {
    return status;
  }

  int fd;
  if ((fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
    io->release();
    return ZX_ERR_BAD_STATE;
  }
  *out_fd = fd;
  return ZX_OK;
}

__EXPORT
zx_status_t fdio_open_fd(const char* path, uint32_t flags, int* out_fd) {
  zx_status_t status = fdio_validate_path(path, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  // Since we are sending a request to the root handle, require that we start at '/'. (In fdio_open
  // above this is done by fdio_ns_connect.)
  if (path[0] != '/') {
    return ZX_ERR_NOT_FOUND;
  }
  path++;

  return fdio_open_fd_common(
      []() {
        fbl::AutoLock lock(&fdio_lock);
        return fdio_root_handle;
      }(),
      path, flags, out_fd);
}

__EXPORT
zx_status_t fdio_open_fd_at(int dir_fd, const char* path, uint32_t flags, int* out_fd) {
  zx_status_t status = fdio_validate_path(path, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  fdio_t* iodir = fdio_unsafe_fd_to_io(dir_fd);
  if (iodir == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  status = fdio_open_fd_common(iodir, path, flags, out_fd);
  fdio_unsafe_release(iodir);
  return status;
}

__EXPORT
zx_handle_t fdio_service_clone(zx_handle_t handle) {
  auto endpoints = fidl::CreateEndpoints<fio::Node>();
  if (endpoints.is_error()) {
    return ZX_HANDLE_INVALID;
  }
  zx_status_t status = fdio_service_clone_to(handle, endpoints->server.channel().release());
  if (status != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  return endpoints->client.channel().release();
}

__EXPORT
zx_status_t fdio_service_clone_to(zx_handle_t handle, zx_handle_t request_raw) {
  auto request = fidl::ServerEnd<fio::Node>(zx::channel(request_raw));
  auto node = fidl::UnownedClientEnd<fio::Node>(handle);
  if (!node.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t flags = ZX_FS_FLAG_CLONE_SAME_RIGHTS;
  return fio::Node::Call::Clone(node, flags, std::move(request)).status();
}

static zx_status_t check_connected(const zx::socket& socket, bool* out_connected) {
  zx_signals_t observed;

  zx_status_t status =
      socket.wait_one(ZXSIO_SIGNAL_CONNECTED, zx::time::infinite_past(), &observed);
  switch (status) {
    case ZX_OK:
      __FALLTHROUGH;
    case ZX_ERR_TIMED_OUT:
      break;
    default:
      return status;
  }
  *out_connected = observed & ZXSIO_SIGNAL_CONNECTED;
  return ZX_OK;
}

zx_status_t fdio_from_node_info(fidl::ClientEnd<fio::Node> node, fio::wire::NodeInfo info,
                                fdio_t** out_io) {
  bool connected = false;

  fdio_t* io = nullptr;
  switch (info.which()) {
    case fio::wire::NodeInfo::Tag::kDirectory: {
      io = fdio_dir_create(fidl::ClientEnd<fio::Directory>(node.TakeChannel()));
    } break;
    case fio::wire::NodeInfo::Tag::kService: {
      io = fdio_remote_create(std::move(node), zx::eventpair{});
    } break;
    case fio::wire::NodeInfo::Tag::kFile: {
      auto& file = info.mutable_file();
      io = fdio_file_create(fidl::ClientEnd<fio::File>(node.TakeChannel()), std::move(file.event),
                            std::move(file.stream));
    } break;
    case fio::wire::NodeInfo::Tag::kDevice: {
      auto& device = info.mutable_device();
      io = fdio_remote_create(std::move(node), std::move(device.event));
    } break;
    case fio::wire::NodeInfo::Tag::kTty: {
      auto& tty = info.mutable_tty();
      io = fdio_pty_create(fidl::ClientEnd<fpty::Device>(node.TakeChannel()), std::move(tty.event));
    } break;
    case fio::wire::NodeInfo::Tag::kVmofile: {
      auto& file = info.mutable_vmofile();
      auto control = fidl::ClientEnd<fio::File>(node.TakeChannel());
      auto result = fio::File::Call::Seek(control.borrow(), 0, fio::wire::SeekOrigin::START);
      zx_status_t status = result.status();
      if (status != ZX_OK) {
        return status;
      }
      status = result->s;
      if (status != ZX_OK) {
        return status;
      }
      io = fdio_vmofile_create(std::move(control), std::move(file.vmo), file.offset, file.length,
                               result->offset);
      break;
    }
    case fio::wire::NodeInfo::Tag::kPipe: {
      auto& pipe = info.mutable_pipe();
      io = fdio_pipe_create(std::move(pipe.socket));
      break;
    }
    case fio::wire::NodeInfo::Tag::kDatagramSocket: {
      auto& socket = info.mutable_datagram_socket();
      io = fdio_datagram_socket_create(
          std::move(socket.event), fidl::ClientEnd<fsocket::DatagramSocket>(node.TakeChannel()));
      break;
    }
    case fio::wire::NodeInfo::Tag::kStreamSocket: {
      auto& socket = info.mutable_stream_socket();
      zx_status_t status = check_connected(socket.socket, &connected);
      if (status != ZX_OK) {
        return status;
      }
      zx_info_socket_t socket_info;
      status = socket.socket.get_info(ZX_INFO_SOCKET, &socket_info, sizeof(socket_info), nullptr,
                                      nullptr);
      if (status != ZX_OK) {
        return status;
      }
      io = fdio_stream_socket_create(std::move(socket.socket),
                                     fidl::ClientEnd<fsocket::StreamSocket>(node.TakeChannel()),
                                     socket_info);
      break;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  if (io == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }

  if (connected) {
    io->ioflag() |= IOFLAG_SOCKET_CONNECTED;
  }

  *out_io = io;
  return ZX_OK;
}

zx_status_t fdio_from_channel(fidl::ClientEnd<fio::Node> node, fdio_t** out_io) {
  auto response = fio::Node::Call::Describe(node);
  zx_status_t status = response.status();
  if (status != ZX_OK) {
    return status;
  }
  return fdio_from_node_info(std::move(node), std::move(response.Unwrap()->info), out_io);
}

__EXPORT
zx_status_t fdio_create(zx_handle_t h, fdio_t** out_io) {
  zx::handle handle(h);
  zx_info_handle_basic_t info = {};
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  fdio_t* io = nullptr;
  switch (info.type) {
    case ZX_OBJ_TYPE_CHANNEL:
      return fdio_from_channel(fidl::ClientEnd<fio::Node>(zx::channel(std::move(handle))), out_io);
    case ZX_OBJ_TYPE_SOCKET:
      io = fdio_pipe_create(zx::socket(std::move(handle)));
      break;
    case ZX_OBJ_TYPE_VMO: {
      zx::vmo vmo(std::move(handle));
      zx::stream stream;
      uint32_t options = 0u;
      if (info.rights & ZX_RIGHT_READ) {
        options |= ZX_STREAM_MODE_READ;
      }
      if (info.rights & ZX_RIGHT_WRITE) {
        options |= ZX_STREAM_MODE_WRITE;
      }
      // We pass 0 for the initial seek value because the |handle| we're given does not remember
      // the seek value we had previously.
      status = zx::stream::create(options, vmo, 0u, &stream);
      if (status != ZX_OK) {
        return status;
      }
      io = fdio_vmo_create(std::move(vmo), std::move(stream));
      break;
    }
    case ZX_OBJ_TYPE_LOG:
      io = fdio_logger_create(zx::debuglog(std::move(handle)));
      break;
    default: {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  if (io == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  *out_io = io;
  return ZX_OK;
}

// Creates an |fdio_t| by waiting for a |fuchsia.io/Node.OnOpen| event on |channel|.
zx_status_t fdio_from_on_open_event(fidl::ClientEnd<fio::Node> client_end, fdio_t** out_io) {
  class EventHandler : public fio::Node::SyncEventHandler {
   public:
    EventHandler(fidl::ClientEnd<fio::Node> client_end, fdio_t** out_io)
        : client_end_(std::move(client_end)), out_io_(out_io) {}

    zx_status_t open_status() const { return open_status_; }

    const fidl::ClientEnd<fio::Node>& client_end() const { return client_end_; }
    void OnOpen(fio::Node::OnOpenResponse* event) override {
      open_status_ = (event->s != ZX_OK) ? event->s
                                         : fdio_from_node_info(std::move(client_end_),
                                                               std::move(event->info), out_io_);
    }

    zx_status_t Unknown() override { return ZX_ERR_IO; }

   private:
    fidl::ClientEnd<fio::Node> client_end_;
    fdio_t** out_io_;
    zx_status_t open_status_ = ZX_OK;
  };

  EventHandler event_handler(std::move(client_end), out_io);
  zx_status_t status = event_handler.HandleOneEvent(event_handler.client_end().borrow()).status();
  if (status == ZX_OK) {
    return event_handler.open_status();
  }
  return (status == ZX_ERR_NOT_SUPPORTED) ? ZX_ERR_IO : status;
}

zx_status_t fdio_remote_clone(fidl::UnownedClientEnd<fio::Node> node, fdio_t** out_io) {
  auto endpoints = fidl::CreateEndpoints<fio::Node>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  zx_status_t status = fio::Node::Call::Clone(
                           node, fio::wire::CLONE_FLAG_SAME_RIGHTS | fio::wire::OPEN_FLAG_DESCRIBE,
                           std::move(endpoints->server))
                           .status();
  if (status != ZX_OK) {
    return status;
  }

  return fdio_from_on_open_event(std::move(endpoints->client), out_io);
}

zx_status_t fdio_remote_open_at(fidl::UnownedClientEnd<fio::Directory> dir, const char* path,
                                uint32_t flags, uint32_t mode, fdio_t** out_io) {
  size_t length;
  zx_status_t status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return status;
  }

  auto endpoints = fidl::CreateEndpoints<fio::Node>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  status = fio::Directory::Call::Open(dir, flags, mode, fidl::unowned_str(path, length),
                                      std::move(endpoints->server))
               .status();
  if (status != ZX_OK) {
    return status;
  }

  if (flags & ZX_FS_FLAG_DESCRIBE) {
    return fdio_from_on_open_event(std::move(endpoints->client), out_io);
  }

  fdio_t* io = fdio_remote_create(std::move(endpoints->client), zx::eventpair{});
  if (io == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }
  *out_io = io;
  return ZX_OK;
}
