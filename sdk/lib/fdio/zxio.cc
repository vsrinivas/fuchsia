// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zxio.h"

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/zxio.h>
#include <poll.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <zircon/device/vfs.h>
#include <zircon/rights.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fbl/auto_lock.h>

#include "fdio_unistd.h"

namespace fio = fuchsia_io;
namespace fpty = fuchsia_hardware_pty;

// Generic ---------------------------------------------------------------------

namespace fdio_internal {

zx_status_t zxio::close() { return zxio_close(&zxio_storage().io); }

zx_status_t zxio::clone(zx_handle_t* out_handle) {
  return zxio_clone(&zxio_storage().io, out_handle);
}

zx_status_t zxio::unwrap(zx_handle_t* out_handle) {
  return zxio_release(&zxio_storage().io, out_handle);
}

void zxio::wait_begin(uint32_t events, zx_handle_t* out_handle, zx_signals_t* out_signals) {
  return wait_begin_inner(events, ZXIO_SIGNAL_NONE, out_handle, out_signals);
}

// TODO(fxbug.dev/45813): This is mainly used by pipes. Consider merging this with the
// POSIX-to-zxio signal translation in |remote::wait_begin|.
// TODO(fxbug.dev/47132): Do not change the signal mapping here and in |wait_end|
// until linked issue is resolved.
void zxio::wait_begin_inner(uint32_t events, zx_signals_t signals, zx_handle_t* out_handle,
                            zx_signals_t* out_signals) {
  if (events & POLLIN) {
    signals |= ZXIO_SIGNAL_READABLE | ZXIO_SIGNAL_PEER_CLOSED | ZXIO_SIGNAL_READ_DISABLED;
  }
  if (events & POLLOUT) {
    signals |= ZXIO_SIGNAL_WRITABLE | ZXIO_SIGNAL_WRITE_DISABLED;
  }
  if (events & POLLRDHUP) {
    signals |= ZXIO_SIGNAL_READ_DISABLED | ZXIO_SIGNAL_PEER_CLOSED;
  }
  zxio_wait_begin(&zxio_storage().io, signals, out_handle, out_signals);
}

void zxio::wait_end(zx_signals_t signals, uint32_t* out_events) {
  return wait_end_inner(signals, out_events, nullptr);
}

void zxio::wait_end_inner(zx_signals_t signals, uint32_t* out_events, zx_signals_t* out_signals) {
  zxio_signals_t zxio_signals;
  zxio_wait_end(&zxio_storage().io, signals, &zxio_signals);
  if (out_signals) {
    *out_signals = zxio_signals;
  }

  uint32_t events = 0;
  if (zxio_signals & (ZXIO_SIGNAL_READABLE | ZXIO_SIGNAL_PEER_CLOSED | ZXIO_SIGNAL_READ_DISABLED)) {
    events |= POLLIN;
  }
  if (zxio_signals & (ZXIO_SIGNAL_WRITABLE | ZXIO_SIGNAL_WRITE_DISABLED)) {
    events |= POLLOUT;
  }
  if (zxio_signals & (ZXIO_SIGNAL_READ_DISABLED | ZXIO_SIGNAL_PEER_CLOSED)) {
    events |= POLLRDHUP;
  }
  *out_events = events;
}

zx_status_t zxio::get_token(zx_handle_t* out) { return zxio_token_get(&zxio_storage().io, out); }

zx_status_t zxio::get_attr(zxio_node_attributes_t* out) {
  return zxio_attr_get(&zxio_storage().io, out);
}

zx_status_t zxio::set_attr(const zxio_node_attributes_t* attr) {
  return zxio_attr_set(&zxio_storage().io, attr);
}

zx_status_t zxio::dirent_iterator_init(zxio_dirent_iterator_t* iterator, zxio_t* directory) {
  return zxio_dirent_iterator_init(iterator, directory);
}

zx_status_t zxio::dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                       zxio_dirent_t** out_entry) {
  return zxio_dirent_iterator_next(iterator, out_entry);
}

void zxio::dirent_iterator_destroy(zxio_dirent_iterator_t* iterator) {
  return zxio_dirent_iterator_destroy(iterator);
}

zx_status_t zxio::unlink(const char* path, size_t len) {
  return zxio_unlink(&zxio_storage().io, path);
}

zx_status_t zxio::truncate(off_t off) { return zxio_truncate(&zxio_storage().io, off); }

zx_status_t zxio::rename(const char* src, size_t srclen, zx_handle_t dst_token, const char* dst,
                         size_t dstlen) {
  return zxio_rename(&zxio_storage().io, src, dst_token, dst);
}

zx_status_t zxio::link(const char* src, size_t srclen, zx_handle_t dst_token, const char* dst,
                       size_t dstlen) {
  return zxio_link(&zxio_storage().io, src, dst_token, dst);
}

zx_status_t zxio::get_flags(uint32_t* out_flags) {
  return zxio_flags_get(&zxio_storage().io, out_flags);
}

zx_status_t zxio::set_flags(uint32_t flags) { return zxio_flags_set(&zxio_storage().io, flags); }

zx_status_t zxio::recvmsg_inner(struct msghdr* msg, int flags, size_t* out_actual) {
  zxio_flags_t zxio_flags = 0;
  if (flags & MSG_PEEK) {
    zxio_flags |= ZXIO_PEEK;
    flags &= ~MSG_PEEK;
  }
  if (flags) {
    // TODO(https://fxbug.dev/67925): support MSG_OOB
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Variable length arrays have to have nonzero sizes, so we can't allocate a zx_iov for an empty
  // io vector. Instead, we can ask to read zero entries with a null vector.
  if (msg->msg_iovlen == 0) {
    return zxio_readv(&zxio_storage().io, nullptr, 0, zxio_flags, out_actual);
  }

  zx_iovec_t zx_iov[msg->msg_iovlen];
  for (int i = 0; i < msg->msg_iovlen; ++i) {
    auto const& iov = msg->msg_iov[i];
    zx_iov[i] = {
        .buffer = iov.iov_base,
        .capacity = iov.iov_len,
    };
  }

  return zxio_readv(&zxio_storage().io, zx_iov, msg->msg_iovlen, zxio_flags, out_actual);
}

zx_status_t zxio::sendmsg_inner(const struct msghdr* msg, int flags, size_t* out_actual) {
  if (flags) {
    // TODO(https://fxbug.dev/67925): support MSG_NOSIGNAL
    // TODO(https://fxbug.dev/67925): support MSG_OOB
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Variable length arrays have to have nonzero sizes, so we can't allocate a zx_iov for an empty
  // io vector. Instead, we can ask to write zero entries with a null vector.
  if (msg->msg_iovlen == 0) {
    return zxio_writev(&zxio_storage().io, nullptr, 0, 0, out_actual);
  }

  zx_iovec_t zx_iov[msg->msg_iovlen];
  for (int i = 0; i < msg->msg_iovlen; ++i) {
    zx_iov[i] = {
        .buffer = msg->msg_iov[i].iov_base,
        .capacity = msg->msg_iov[i].iov_len,
    };
  }
  return zxio_writev(&zxio_storage().io, zx_iov, msg->msg_iovlen, 0, out_actual);
}

zx_status_t zxio::recvmsg(struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
  *out_code = 0;
  return recvmsg_inner(msg, flags, out_actual);
}

zx_status_t zxio::sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                          int16_t* out_code) {
  *out_code = 0;
  return sendmsg_inner(msg, flags, out_actual);
}

}  // namespace fdio_internal

__EXPORT
fdio_t* fdio_zxio_create(zxio_storage_t** out_storage) {
  auto* io = fdio_internal::alloc<fdio_internal::zxio>();
  if (io == nullptr) {
    return nullptr;
  }
  zxio_storage_t& storage = io->zxio_storage();
  zxio_null_init(&storage.io);
  *out_storage = &storage;
  return io;
}

// Null ------------------------------------------------------------------------

__EXPORT
fdio_t* fdio_null_create(void) {
  zxio_storage_t* storage = nullptr;
  return fdio_zxio_create(&storage);
}

__EXPORT
int fdio_fd_create_null(void) { return fdio_bind_to_fd(fdio_null_create(), -1, 0); }

// Remote ----------------------------------------------------------------------

namespace fdio_internal {

struct remote : public zxio {
  zx_status_t open(const char* path, uint32_t flags, uint32_t mode, fdio_t** out) override {
    size_t length;
    zx_status_t status = fdio_validate_path(path, &length);
    if (status != ZX_OK) {
      return status;
    }

    auto endpoints = fidl::CreateEndpoints<fio::Node>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }

    status = zxio_open_async(&zxio_storage().io, flags, mode, path, length,
                             endpoints->server.channel().release());
    if (status != ZX_OK) {
      return status;
    }

    if (flags & ZX_FS_FLAG_DESCRIBE) {
      return fdio_from_on_open_event(std::move(endpoints->client), out);
    }

    fdio_t* remote_io = fdio_remote_create(std::move(endpoints->client), zx::eventpair{});
    if (remote_io == nullptr) {
      return ZX_ERR_NO_RESOURCES;
    }
    *out = remote_io;
    return ZX_OK;
  }

  zx_status_t borrow_channel(zx_handle_t* out_borrowed) override {
    *out_borrowed = zxio_remote().control;
    return ZX_OK;
  }

  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* signals) override {
    // POLLERR is always detected.
    events |= POLLERR;
    zxio_signals_t zxio_signals = poll_events_to_zxio_signals(events);
    zxio_wait_begin(&zxio_storage().io, zxio_signals, handle, signals);
  }

  void wait_end(zx_signals_t signals, uint32_t* events) override {
    zxio_signals_t zxio_signals = 0;
    zxio_wait_end(&zxio_storage().io, signals, &zxio_signals);
    *events = zxio_signals_to_poll_events(zxio_signals);
  }

 protected:
  friend class AllocHelper<remote>;

  remote() = default;
  ~remote() override = default;

  const zxio_remote_t& zxio_remote() {
    return *reinterpret_cast<zxio_remote_t*>(&zxio_storage().io);
  }

 private:
  static zxio_signals_t poll_events_to_zxio_signals(uint32_t events) {
    zxio_signals_t signals = ZXIO_SIGNAL_NONE;
    if (events & POLLIN) {
      signals |= ZXIO_SIGNAL_READABLE;
    }
    if (events & POLLPRI) {
      signals |= ZXIO_SIGNAL_OUT_OF_BAND;
    }
    if (events & POLLOUT) {
      signals |= ZXIO_SIGNAL_WRITABLE;
    }
    if (events & POLLERR) {
      signals |= ZXIO_SIGNAL_ERROR;
    }
    if (events & POLLHUP) {
      signals |= ZXIO_SIGNAL_PEER_CLOSED;
    }
    if (events & POLLRDHUP) {
      signals |= ZXIO_SIGNAL_READ_DISABLED;
    }
    return signals;
  }

  static zxio_signals_t zxio_signals_to_poll_events(zxio_signals_t signals) {
    uint32_t events = 0;
    if (signals & ZXIO_SIGNAL_READABLE) {
      events |= POLLIN;
    }
    if (signals & ZXIO_SIGNAL_OUT_OF_BAND) {
      events |= POLLPRI;
    }
    if (signals & ZXIO_SIGNAL_WRITABLE) {
      events |= POLLOUT;
    }
    if (signals & ZXIO_SIGNAL_ERROR) {
      events |= POLLERR;
    }
    if (signals & ZXIO_SIGNAL_PEER_CLOSED) {
      events |= POLLHUP;
    }
    if (signals & ZXIO_SIGNAL_READ_DISABLED) {
      events |= POLLRDHUP;
    }
    return events;
  }
};

}  // namespace fdio_internal

fdio_t* fdio_remote_create(fidl::ClientEnd<fio::Node> node, zx::eventpair event) {
  auto* io = fdio_internal::alloc<fdio_internal::remote>();
  if (io == nullptr) {
    return nullptr;
  }
  zx_status_t status =
      zxio_remote_init(&io->zxio_storage(), node.channel().release(), event.release());
  if (status != ZX_OK) {
    return nullptr;
  }
  return io;
}

namespace fdio_internal {

struct dir : public remote {
  // Override |convert_to_posix_mode| for directories, since directories
  // have different semantics for the "rwx" bits.
  uint32_t convert_to_posix_mode(zxio_node_protocols_t protocols,
                                 zxio_abilities_t abilities) override {
    return zxio_node_protocols_to_posix_type(protocols) |
           zxio_abilities_to_posix_permissions_for_directory(abilities);
  }

 protected:
  friend class AllocHelper<dir>;

  dir() = default;
  ~dir() override = default;
};

}  // namespace fdio_internal

fdio_t* fdio_dir_create(fidl::ClientEnd<fio::Directory> dir) {
  auto* io = fdio_internal::alloc<fdio_internal::dir>();
  if (io == nullptr) {
    return nullptr;
  }
  zx_status_t status = zxio_dir_init(&io->zxio_storage(), dir.channel().release());
  if (status != ZX_OK) {
    return nullptr;
  }
  return io;
}

fdio_t* fdio_file_create(fidl::ClientEnd<fio::File> file, zx::event event, zx::stream stream) {
  auto* io = fdio_internal::alloc<fdio_internal::remote>();
  if (io == nullptr) {
    return nullptr;
  }
  zx_status_t status = zxio_file_init(&io->zxio_storage(), file.channel().release(),
                                      event.release(), stream.release());
  if (status != ZX_OK) {
    return nullptr;
  }
  return io;
}

namespace fdio_internal {

struct pty : public remote {
  Errno posix_ioctl(int request, va_list va) override {
    switch (request) {
      case TIOCGWINSZ: {
        fidl::UnownedClientEnd<fpty::Device> device(zxio_remote().control);
        if (!device.is_valid()) {
          return Errno(ENOTTY);
        }

        auto result = fpty::Device::Call::GetWindowSize(device);
        if (result.status() != ZX_OK || result->status != ZX_OK) {
          return Errno(ENOTTY);
        }

        struct winsize size = {
            .ws_row = static_cast<uint16_t>(result->size.height),
            .ws_col = static_cast<uint16_t>(result->size.width),
        };
        struct winsize* out_size = va_arg(va, struct winsize*);
        *out_size = size;
        return Errno(Errno::Ok);
      }
      case TIOCSWINSZ: {
        fidl::UnownedClientEnd<fpty::Device> device(zxio_remote().control);
        if (!device.is_valid()) {
          return Errno(ENOTTY);
        }

        const struct winsize* in_size = va_arg(va, const struct winsize*);
        fpty::wire::WindowSize size = {};
        size.width = in_size->ws_col;
        size.height = in_size->ws_row;

        auto result = fpty::Device::Call::SetWindowSize(device, size);
        if (result.status() != ZX_OK || result->status != ZX_OK) {
          return Errno(ENOTTY);
        }
        return Errno(Errno::Ok);
      }
      default:
        return Errno(ENOTTY);
    }
  }

 protected:
  friend class AllocHelper<pty>;

  pty() = default;
  ~pty() override = default;
};

}  // namespace fdio_internal

fdio_t* fdio_pty_create(fidl::ClientEnd<fpty::Device> device, zx::eventpair event) {
  auto* io = fdio_internal::alloc<fdio_internal::pty>();
  if (io == nullptr) {
    return nullptr;
  }
  zx_status_t status =
      zxio_remote_init(&io->zxio_storage(), device.channel().release(), event.release());
  if (status != ZX_OK) {
    io->release();
    return nullptr;
  }
  return io;
}

__EXPORT
zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out) {
  fdio_t* io;
  zx_status_t status = fdio_unbind_from_fd(fd, &io);
  if (status != ZX_OK) {
    if (status == ZX_ERR_INVALID_ARGS) {
      status = ZX_ERR_NOT_FOUND;
    }
    return status;
  }
  auto* base = reinterpret_cast<fdio_internal::base*>(io);
  status = base->unwrap(out);
  base->release();
  return status;
}

__EXPORT
zx_handle_t fdio_unsafe_borrow_channel(fdio_t* io) {
  if (io == nullptr) {
    return ZX_HANDLE_INVALID;
  }

  auto* base = reinterpret_cast<fdio_internal::base*>(io);
  zx_handle_t handle = ZX_HANDLE_INVALID;
  if (base->borrow_channel(&handle) != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  return handle;
}

// Vmo -------------------------------------------------------------------------

fdio_t* fdio_vmo_create(zx::vmo vmo, zx::stream stream) {
  auto* io = fdio_internal::alloc<fdio_internal::zxio>();
  if (io == nullptr) {
    return nullptr;
  }
  zx_status_t status = zxio_vmo_init(&io->zxio_storage(), std::move(vmo), std::move(stream));
  if (status != ZX_OK) {
    io->release();
    return nullptr;
  }
  return io;
}

// Vmofile ---------------------------------------------------------------------

fdio_t* fdio_vmofile_create(fidl::ClientEnd<fio::File> file, zx::vmo vmo, zx_off_t offset,
                            zx_off_t length, zx_off_t seek) {
  // NB: vmofile doesn't support some of the operations, but it can fail in zxio.
  auto* io = fdio_internal::alloc<fdio_internal::zxio>();
  if (io == nullptr) {
    return nullptr;
  }
  zx_status_t status = zxio_vmofile_init(&io->zxio_storage(), fidl::BindSyncClient(std::move(file)),
                                         std::move(vmo), offset, length, seek);
  if (status != ZX_OK) {
    return nullptr;
  }
  return io;
}

// Pipe ------------------------------------------------------------------------

namespace fdio_internal {

Errno pipe::posix_ioctl(int request, va_list va) {
  return posix_ioctl_inner(zxio_pipe().socket, request, va);
}

Errno pipe::posix_ioctl_inner(const zx::socket& socket, int request, va_list va) {
  switch (request) {
    case FIONREAD: {
      zx_info_socket_t info;
      memset(&info, 0, sizeof(info));
      zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
      if (status != ZX_OK) {
        return Errno(fdio_status_to_errno(status));
      }
      size_t available = info.rx_buf_available;
      if (available > INT_MAX) {
        available = INT_MAX;
      }
      int* actual = va_arg(va, int*);
      *actual = static_cast<int>(available);
      return Errno(Errno::Ok);
    }
    default:
      return Errno(ENOTTY);
  }
}

zx_status_t pipe::shutdown(int how, int16_t* out_code) {
  *out_code = 0;
  return shutdown_inner(zxio_pipe().socket, how);
}

zx_status_t pipe::shutdown_inner(const zx::socket& socket, int how) {
  uint32_t options;
  switch (how) {
    case SHUT_RD:
      options = ZX_SOCKET_SHUTDOWN_READ;
      break;
    case SHUT_WR:
      options = ZX_SOCKET_SHUTDOWN_WRITE;
      break;
    case SHUT_RDWR:
      options = ZX_SOCKET_SHUTDOWN_READ | ZX_SOCKET_SHUTDOWN_WRITE;
      break;
  }
  return socket.shutdown(options);
}

const zxio_pipe_t& pipe::zxio_pipe() { return *reinterpret_cast<zxio_pipe_t*>(&zxio_storage().io); }

}  // namespace fdio_internal

fdio_t* fdio_pipe_create(zx::socket socket) {
  auto* io = fdio_internal::alloc<fdio_internal::pipe>();
  if (io == nullptr) {
    return nullptr;
  }
  zx_info_socket_t info;
  zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    io->release();
    return nullptr;
  }
  status = zxio_pipe_init(&io->zxio_storage(), std::move(socket), info);
  if (status != ZX_OK) {
    io->release();
    return nullptr;
  }
  return io;
}

zx_status_t fdio_pipe_pair(fdio_t** _a, fdio_t** _b, uint32_t options) {
  zx::socket h0, h1;
  zx_status_t status = zx::socket::create(options, &h0, &h1);
  if (status != ZX_OK) {
    return status;
  }
  fdio_t* a = fdio_pipe_create(std::move(h0));
  if (a == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  fdio_t* b = fdio_pipe_create(std::move(h1));
  if (b == nullptr) {
    a->release();
    return ZX_ERR_NO_MEMORY;
  }
  *_a = a;
  *_b = b;
  return 0;
}

__EXPORT
zx_status_t fdio_pipe_half(int* out_fd, zx_handle_t* out_handle) {
  zx::socket h0, h1;
  zx_status_t status = zx::socket::create(0, &h0, &h1);
  if (status != ZX_OK) {
    return status;
  }
  fdio_t* io = fdio_pipe_create(std::move(h0));
  if (io == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  if ((*out_fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
    io->release();
    return ZX_ERR_NO_RESOURCES;
  }
  *out_handle = h1.release();
  return ZX_OK;
}
