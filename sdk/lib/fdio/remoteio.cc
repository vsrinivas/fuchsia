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

#include "lib/zx/channel.h"
#include "private-socket.h"

namespace fio = ::llcpp::fuchsia::io;
namespace fsocket = ::llcpp::fuchsia::posix::socket;
namespace fdevice = ::llcpp::fuchsia::device;

#define ZXDEBUG 0

static_assert(FDIO_CHUNK_SIZE >= PATH_MAX, "FDIO_CHUNK_SIZE must be large enough to contain paths");

static_assert(fio::VMO_FLAG_READ == ZX_VM_PERM_READ, "Vmar / Vmo flags should be aligned");
static_assert(fio::VMO_FLAG_WRITE == ZX_VM_PERM_WRITE, "Vmar / Vmo flags should be aligned");
static_assert(fio::VMO_FLAG_EXEC == ZX_VM_PERM_EXECUTE, "Vmar / Vmo flags should be aligned");

static_assert(fio::DEVICE_SIGNAL_READABLE == fdevice::DEVICE_SIGNAL_READABLE);
static_assert(fio::DEVICE_SIGNAL_OOB == fdevice::DEVICE_SIGNAL_OOB);
static_assert(fio::DEVICE_SIGNAL_WRITABLE == fdevice::DEVICE_SIGNAL_WRITABLE);
static_assert(fio::DEVICE_SIGNAL_ERROR == fdevice::DEVICE_SIGNAL_ERROR);
static_assert(fio::DEVICE_SIGNAL_HANGUP == fdevice::DEVICE_SIGNAL_HANGUP);

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
  zx::channel request(request_raw);
  size_t length = 0u;
  zx_status_t status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return status;
  }

  if (dir == ZX_HANDLE_INVALID) {
    return ZX_ERR_UNAVAILABLE;
  }
  uint32_t flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;
  return fio::Directory::Call::Open(zx::unowned_channel(dir), flags, FDIO_CONNECT_MODE,
                                    fidl::unowned_str(path, length), std::move(request))
      .status();
}

zx_status_t fdio_service_connect_by_name(const char name[], zx::channel* out) {
  static zx_handle_t service_root;

  {
    static std::once_flag once;
    static zx_status_t status;
    std::call_once(once, [&]() {
      zx::channel c0, c1;
      status = zx::channel::create(0, &c0, &c1);
      if (status != ZX_OK) {
        return;
      }
      // TODO(abarth): Use "/svc/" once that actually works.
      status = fdio_service_connect("/svc/.", c0.release());
      if (status != ZX_OK) {
        return;
      }
      service_root = c1.release();
    });
    if (status != ZX_OK) {
      return status;
    }
  }

  zx::channel c0, c1;
  zx_status_t status = zx::channel::create(0, &c0, &c1);
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_service_connect_at(service_root, name, c0.release());
  if (status != ZX_OK) {
    return status;
  }
  *out = std::move(c1);
  return ZX_OK;
}

__EXPORT
zx_status_t fdio_open(const char* path, uint32_t flags, zx_handle_t request) {
  // TODO: fdio_validate_path?
  if (path == nullptr) {
    zx_handle_close(request);
    return ZX_ERR_INVALID_ARGS;
  }
  // Otherwise attempt to connect through the root namespace
  return fdio_ns_connect(fdio_root_ns, path, flags, request);
}

__EXPORT
zx_status_t fdio_open_at(zx_handle_t dir, const char* path, uint32_t flags,
                         zx_handle_t raw_request) {
  zx::channel request(raw_request);
  size_t length;
  zx_status_t status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return status;
  }

  if (flags & ZX_FS_FLAG_DESCRIBE) {
    return ZX_ERR_INVALID_ARGS;
  }

  return fio::Directory::Call::Open(zx::unowned_channel(dir), flags, FDIO_CONNECT_MODE,
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
  zx_status_t status = fdio_get_ops(iodir)->open(iodir, path, flags, FDIO_CONNECT_MODE, &io);
  if (status != ZX_OK) {
    return status;
  }

  int fd;
  if ((fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
    fdio_get_ops(io)->close(io);
    fdio_release(io);
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

  return fdio_open_fd_common(fdio_root_handle, path, flags, out_fd);
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
  if (handle == ZX_HANDLE_INVALID) {
    return ZX_HANDLE_INVALID;
  }
  zx::channel clone, request;
  if (zx::channel::create(0, &clone, &request) != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  uint32_t flags = ZX_FS_FLAG_CLONE_SAME_RIGHTS;
  auto result = fio::Node::Call::Clone(zx::unowned_channel(handle), flags, std::move(request));
  if (result.status() != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  return clone.release();
}

__EXPORT
zx_status_t fdio_service_clone_to(zx_handle_t handle, zx_handle_t request_raw) {
  zx::channel request(request_raw);
  if (!request.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t flags = ZX_FS_FLAG_CLONE_SAME_RIGHTS;
  return fio::Node::Call::Clone(zx::unowned_channel(handle), flags, std::move(request)).status();
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

zx_status_t fdio_from_node_info(zx::channel handle, fio::NodeInfo info, fdio_t** out_io) {
  if (!handle.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }

  bool connected = false;

  fdio_t* io = nullptr;
  switch (info.which()) {
    case fio::NodeInfo::Tag::kDirectory:
      io = fdio_dir_create(handle.release());
      break;
    case fio::NodeInfo::Tag::kService:
      io = fdio_remote_create(handle.release(), 0);
      break;
    case fio::NodeInfo::Tag::kFile:
      io = fdio_file_create(handle.release(), info.mutable_file().event.release(),
                            info.mutable_file().stream.release());
      break;
    case fio::NodeInfo::Tag::kDevice:
      io = fdio_remote_create(handle.release(), info.mutable_device().event.release());
      break;
    case fio::NodeInfo::Tag::kTty:
      io = fdio_remote_create(handle.release(), info.mutable_tty().event.release());
      break;
    case fio::NodeInfo::Tag::kVmofile: {
      fio::File::SyncClient control(std::move(handle));
      auto result = control.Seek(0, fio::SeekOrigin::START);
      zx_status_t status = result.status();
      if (status != ZX_OK) {
        return status;
      }
      status = result->s;
      if (status != ZX_OK) {
        return status;
      }
      io = fdio_vmofile_create(std::move(control), std::move(info.mutable_vmofile().vmo),
                               info.vmofile().offset, info.vmofile().length, result->offset);
      break;
    }
    case fio::NodeInfo::Tag::kPipe: {
      io = fdio_pipe_create(std::move(info.mutable_pipe().socket));
      break;
    }
    case fio::NodeInfo::Tag::kDatagramSocket: {
      io = fdio_datagram_socket_create(std::move(info.mutable_datagram_socket().event),
                                       fsocket::DatagramSocket::SyncClient(std::move(handle)));
      break;
    }
    case fio::NodeInfo::Tag::kStreamSocket: {
      zx_status_t status = check_connected(info.stream_socket().socket, &connected);
      if (status != ZX_OK) {
        return status;
      }
      zx_info_socket_t socket_info;
      status = info.stream_socket().socket.get_info(ZX_INFO_SOCKET, &socket_info,
                                                    sizeof(socket_info), nullptr, nullptr);
      if (status != ZX_OK) {
        return status;
      }
      io = fdio_stream_socket_create(std::move(info.mutable_stream_socket().socket),
                                     fsocket::StreamSocket::SyncClient(std::move(handle)),
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
    *fdio_get_ioflag(io) |= IOFLAG_SOCKET_CONNECTED;
  }

  *out_io = io;
  return ZX_OK;
}

zx_status_t fdio_from_channel(zx::channel channel, fdio_t** out_io) {
  auto response = fio::Node::Call::Describe(zx::unowned_channel(channel));
  zx_status_t status = response.status();
  if (status != ZX_OK) {
    return status;
  }
  return fdio_from_node_info(std::move(channel), std::move(response.Unwrap()->info), out_io);
}

__EXPORT
zx_status_t fdio_create(zx_handle_t handle, fdio_t** out_io) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }
  fdio_t* io = nullptr;
  switch (info.type) {
    case ZX_OBJ_TYPE_CHANNEL:
      return fdio_from_channel(zx::channel(handle), out_io);
    case ZX_OBJ_TYPE_SOCKET:
      io = fdio_pipe_create(zx::socket(handle));
      break;
    case ZX_OBJ_TYPE_VMO:
      io = fdio_vmo_create(zx::vmo(handle), 0u);
      break;
    case ZX_OBJ_TYPE_LOG:
      io = fdio_logger_create(zx::debuglog(handle));
      break;
    default: {
      zx_handle_close(handle);
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
zx_status_t fdio_from_on_open_event(zx::channel channel, fdio_t** out_io) {
  // HandleEvents will read an event message from its first parameter, then call
  // one of the callbacks in EventHandlers to handle the event data, so its first
  // parameter is no longer needed once it gets to the callback. We need to extract
  // the underlying handle from |channel|, then use it to create the unowned
  // channel used by HandleEvents, since the handle may be moved out of |channel|
  // before the first parameter to HandleEvents is evaluated.
  zx_handle_t event_channel_handle = channel.get();
  fio::Directory::EventHandlers event_handlers{
      .on_open =
          [channel = std::move(channel), out_io](fio::Directory::OnOpenResponse* message) mutable {
            if (message->s != ZX_OK) {
              return message->s;
            }
            return fdio_from_node_info(std::move(channel), std::move(message->info), out_io);
          },
      .unknown = [] { return ZX_ERR_IO; }};
  return fio::Directory::Call::HandleEvents(zx::unowned_channel(event_channel_handle),
                                            event_handlers)
      .status();
}

zx_status_t fdio_remote_clone(zx_handle_t node, fdio_t** out_io) {
  zx::channel handle, request;
  zx_status_t status = zx::channel::create(0, &handle, &request);
  if (status != ZX_OK) {
    return status;
  }

  status = fio::Node::Call::Clone(zx::unowned_channel(node),
                                  fio::CLONE_FLAG_SAME_RIGHTS | fio::OPEN_FLAG_DESCRIBE,
                                  std::move(request))
               .status();
  if (status != ZX_OK) {
    return status;
  }

  return fdio_from_on_open_event(std::move(handle), out_io);
}

zx_status_t fdio_remote_open_at(zx_handle_t dir, const char* path, uint32_t flags, uint32_t mode,
                                fdio_t** out_io) {
  size_t length;
  zx_status_t status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return status;
  }

  zx::channel handle, request;
  status = zx::channel::create(0, &handle, &request);
  if (status != ZX_OK) {
    return status;
  }

  status = fio::Directory::Call::Open(zx::unowned_channel(dir), flags, mode,
                                      fidl::unowned_str(path, length), std::move(request))
               .status();
  if (status != ZX_OK) {
    return status;
  }

  if (flags & ZX_FS_FLAG_DESCRIBE) {
    return fdio_from_on_open_event(std::move(handle), out_io);
  }

  fdio_t* io = fdio_remote_create(handle.release(), 0);
  if (io == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }
  *out_io = io;
  return ZX_OK;
}
