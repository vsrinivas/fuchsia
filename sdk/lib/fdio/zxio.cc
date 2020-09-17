// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

#include "private.h"

namespace fio = ::llcpp::fuchsia::io;

static zx_status_t fdio_zxio_open(fdio_t* io, const char* path, uint32_t flags, uint32_t mode,
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

  zxio_t* z = fdio_get_zxio(io);
  status = zxio_open_async(z, flags, mode, path, length, request.release());
  if (status != ZX_OK) {
    return status;
  }

  if (flags & ZX_FS_FLAG_DESCRIBE) {
    return fdio_from_on_open_event(std::move(handle), out_io);
  }

  fdio_t* remote_io = fdio_remote_create(handle.release(), 0);
  if (remote_io == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }
  *out_io = remote_io;
  return ZX_OK;
}

zx_status_t fdio_zxio_close(fdio_t* io) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_close(z);
}

// TODO(fxbug.dev/45813): This is mainly used by pipes. Consider merging this with the
// POSIX-to-zxio signal translation in |fdio_zxio_remote_wait_begin|.
// TODO(fxbug.dev/47132): Do not change the signal mapping here and in |fdio_zxio_wait_end|
// until linked issue is resolved.
static void fdio_zxio_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* out_handle,
                                 zx_signals_t* out_signals) {
  zxio_t* z = fdio_get_zxio(io);
  zxio_signals_t signals = ZXIO_SIGNAL_NONE;
  if (events & POLLIN) {
    signals |= ZXIO_SIGNAL_READABLE | ZXIO_SIGNAL_PEER_CLOSED | ZXIO_SIGNAL_READ_DISABLED;
  }
  if (events & POLLOUT) {
    signals |= ZXIO_SIGNAL_WRITABLE | ZXIO_SIGNAL_WRITE_DISABLED;
  }
  if (events & POLLRDHUP) {
    signals |= ZXIO_SIGNAL_READ_DISABLED | ZXIO_SIGNAL_PEER_CLOSED;
  }
  zxio_wait_begin(z, signals, out_handle, out_signals);
}

static void fdio_zxio_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* out_events) {
  zxio_t* z = fdio_get_zxio(io);
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  zxio_wait_end(z, signals, &zxio_signals);

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

zx_status_t fdio_zxio_clone(fdio_t* io, zx_handle_t* out_handle) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_clone(z, out_handle);
}

zx_status_t fdio_zxio_unwrap(fdio_t* io, zx_handle_t* out_handle) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_release(z, out_handle);
}

static zx_status_t fdio_zxio_get_attr(fdio_t* io, zxio_node_attributes_t* out) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_attr_get(z, out);
}

static zx_status_t fdio_zxio_set_attr(fdio_t* io, const zxio_node_attributes_t* attr) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_attr_set(z, attr);
}

static zx_status_t fdio_zxio_truncate(fdio_t* io, off_t off) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_truncate(z, off);
}

static zx_status_t fdio_zxio_get_flags(fdio_t* io, uint32_t* out_flags) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_flags_get(z, out_flags);
}

static zx_status_t fdio_zxio_set_flags(fdio_t* io, uint32_t flags) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_flags_set(z, flags);
}

static zx_status_t fdio_zxio_get_token(fdio_t* io, zx_handle_t* out_token) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_token_get(z, out_token);
}

static zx_status_t fdio_zxio_rename(fdio_t* io, const char* src, size_t srclen,
                                    zx_handle_t dst_token, const char* dst, size_t dstlen) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_rename(z, src, dst_token, dst);
}

static zx_status_t fdio_zxio_unlink(fdio_t* io, const char* path, size_t len) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_unlink(z, path);
}

static zx_status_t fdio_zxio_link(fdio_t* io, const char* src, size_t srclen, zx_handle_t dst_token,
                                  const char* dst, size_t dstlen) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_link(z, src, dst_token, dst);
}

static zx_status_t fdio_zxio_dirent_iterator_init(fdio_t* io, zxio_dirent_iterator_t* iterator,
                                                  zxio_t* directory) {
  return zxio_dirent_iterator_init(iterator, directory);
}

static zx_status_t fdio_zxio_dirent_iterator_next(fdio_t* io, zxio_dirent_iterator_t* iterator,
                                                  zxio_dirent_t** out_entry) {
  return zxio_dirent_iterator_next(iterator, out_entry);
}

static void fdio_zxio_dirent_iterator_destroy(fdio_t* io, zxio_dirent_iterator_t* iterator) {
  return zxio_dirent_iterator_destroy(iterator);
}

// Generic ---------------------------------------------------------------------

static zx_status_t fdio_default_accept(fdio_t* io, int flags, struct sockaddr* addr,
                                       socklen_t* addrlen, zx_handle_t* out_handle,
                                       int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

static constexpr fdio_ops_t fdio_zxio_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .borrow_channel = fdio_default_borrow_channel,
    .wait_begin = fdio_zxio_wait_begin,
    .wait_end = fdio_zxio_wait_end,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .convert_to_posix_mode = fdio_default_convert_to_posix_mode,
    .dirent_iterator_init = fdio_zxio_dirent_iterator_init,
    .dirent_iterator_next = fdio_zxio_dirent_iterator_next,
    .dirent_iterator_destroy = fdio_zxio_dirent_iterator_destroy,
    .unlink = fdio_default_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_zxio_get_flags,
    .set_flags = fdio_zxio_set_flags,
    .bind = fdio_default_bind,
    .connect = fdio_default_connect,
    .listen = fdio_default_listen,
    .accept = fdio_default_accept,
    .getsockname = fdio_default_getsockname,
    .getpeername = fdio_default_getpeername,
    .getsockopt = fdio_default_getsockopt,
    .setsockopt = fdio_default_setsockopt,
    .recvmsg = fdio_zxio_recvmsg,
    .sendmsg = fdio_zxio_sendmsg,
    .shutdown = fdio_default_shutdown,
};

__EXPORT
fdio_t* fdio_zxio_create(zxio_storage_t** out_storage) {
  fdio_t* io = fdio_alloc(&fdio_zxio_ops);
  if (io == nullptr) {
    return nullptr;
  }
  zxio_null_init(&fdio_get_zxio_storage(io)->io);
  *out_storage = fdio_get_zxio_storage(io);
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

static zxio_remote_t* fdio_get_zxio_remote(fdio_t* io) { return (zxio_remote_t*)fdio_get_zxio(io); }

static zx_status_t fdio_zxio_remote_borrow_channel(fdio_t* io, zx_handle_t* out_borrowed) {
  zxio_remote_t* remote = fdio_get_zxio_remote(io);
  *out_borrowed = remote->control;
  return ZX_OK;
}

static void fdio_zxio_remote_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle,
                                        zx_signals_t* signals) {
  // POLLERR is always detected
  events |= POLLERR;
  zxio_signals_t zxio_signals = poll_events_to_zxio_signals(events);
  zxio_wait_begin(fdio_get_zxio(io), zxio_signals, handle, signals);
}

static void fdio_zxio_remote_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* events) {
  zxio_signals_t zxio_signals = 0;
  zxio_wait_end(fdio_get_zxio(io), signals, &zxio_signals);
  *events = zxio_signals_to_poll_events(zxio_signals);
}

static constexpr fdio_ops_t fdio_zxio_remote_ops = {
    .close = fdio_zxio_close,
    .open = fdio_zxio_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .borrow_channel = fdio_zxio_remote_borrow_channel,
    .wait_begin = fdio_zxio_remote_wait_begin,
    .wait_end = fdio_zxio_remote_wait_end,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_token = fdio_zxio_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .convert_to_posix_mode = fdio_default_convert_to_posix_mode,
    .dirent_iterator_init = fdio_zxio_dirent_iterator_init,
    .dirent_iterator_next = fdio_zxio_dirent_iterator_next,
    .dirent_iterator_destroy = fdio_zxio_dirent_iterator_destroy,
    .unlink = fdio_zxio_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_zxio_rename,
    .link = fdio_zxio_link,
    .get_flags = fdio_zxio_get_flags,
    .set_flags = fdio_zxio_set_flags,
    .bind = fdio_default_bind,
    .connect = fdio_default_connect,
    .listen = fdio_default_listen,
    .accept = fdio_default_accept,
    .getsockname = fdio_default_getsockname,
    .getpeername = fdio_default_getpeername,
    .getsockopt = fdio_default_getsockopt,
    .setsockopt = fdio_default_setsockopt,
    .recvmsg = fdio_zxio_recvmsg,
    .sendmsg = fdio_zxio_sendmsg,
    .shutdown = fdio_default_shutdown,
};

fdio_t* fdio_remote_create(zx_handle_t control, zx_handle_t event) {
  fdio_t* io = fdio_alloc(&fdio_zxio_remote_ops);
  if (io == nullptr) {
    zx_handle_close(control);
    zx_handle_close(event);
    return nullptr;
  }
  zx_status_t status = zxio_remote_init(fdio_get_zxio_storage(io), control, event);
  if (status != ZX_OK) {
    return nullptr;
  }
  return io;
}

uint32_t fdio_dir_convert_to_posix_mode(fdio_t* io, zxio_node_protocols_t protocols,
                                        zxio_abilities_t abilities) {
  return zxio_node_protocols_to_posix_type(protocols) |
         zxio_abilities_to_posix_permissions_for_directory(abilities);
}

static constexpr fdio_ops_t fdio_zxio_dir_ops = ([] {
  fdio_ops_t remote_ops = fdio_zxio_remote_ops;
  // Override |convert_to_posix_mode| for directories, since directories
  // have different semantics for the "rwx" bits.
  remote_ops.convert_to_posix_mode = fdio_dir_convert_to_posix_mode;
  return remote_ops;
})();

fdio_t* fdio_dir_create(zx_handle_t control) {
  fdio_t* io = fdio_alloc(&fdio_zxio_dir_ops);
  if (io == nullptr) {
    zx_handle_close(control);
    return nullptr;
  }
  zx_status_t status = zxio_dir_init(fdio_get_zxio_storage(io), control);
  if (status != ZX_OK) {
    return nullptr;
  }
  return io;
}

fdio_t* fdio_file_create(zx_handle_t control, zx_handle_t event, zx_handle_t stream) {
  fdio_t* io = fdio_alloc(&fdio_zxio_remote_ops);
  if (io == nullptr) {
    zx_handle_close(control);
    zx_handle_close(event);
    zx_handle_close(stream);
    return nullptr;
  }
  zx_status_t status = zxio_file_init(fdio_get_zxio_storage(io), control, event, stream);
  if (status != ZX_OK) {
    return nullptr;
  }
  return io;
}

__EXPORT
zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out) {
  mtx_lock(&fdio_lock);
  if ((fd < 0) || (fd >= FDIO_MAX_FD) || (fdio_fdtab[fd] == nullptr)) {
    mtx_unlock(&fdio_lock);
    return ZX_ERR_NOT_FOUND;
  }
  fdio_t* io = fdio_fdtab[fd];
  fdio_dupcount_release(io);
  fdio_fdtab[fd] = nullptr;
  if (fdio_get_dupcount(io) > 0) {
    // still alive in other fdtab slots
    // this fd goes away but we can't give away the handle
    mtx_unlock(&fdio_lock);
    fdio_release(io);
    return ZX_ERR_UNAVAILABLE;
  } else {
    mtx_unlock(&fdio_lock);
    zx_status_t r = fdio_get_ops(io)->unwrap(io, out);
    if (r != ZX_OK) {
      fdio_get_ops(io)->close(io);
    }
    fdio_release(io);
    return r;
  }
}

__EXPORT
zx_handle_t fdio_unsafe_borrow_channel(fdio_t* io) {
  if (io == nullptr) {
    return ZX_HANDLE_INVALID;
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  if (fdio_get_ops(io)->borrow_channel(io, &handle) != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  return handle;
}

// Vmo -------------------------------------------------------------------------

fdio_t* fdio_vmo_create(zx::vmo vmo, zx_off_t seek) {
  zxio_storage_t* storage;
  fdio_t* io = fdio_zxio_create(&storage);
  if (io == nullptr) {
    return nullptr;
  }
  zx_status_t status = zxio_vmo_init(storage, std::move(vmo), seek);
  if (status != ZX_OK) {
    fdio_release(io);
    return nullptr;
  }
  return io;
}

// Vmofile ---------------------------------------------------------------------

static constexpr fdio_ops_t fdio_zxio_vmofile_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .borrow_channel = fdio_default_borrow_channel,
    .wait_begin = fdio_default_wait_begin,
    .wait_end = fdio_default_wait_end,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .convert_to_posix_mode = fdio_default_convert_to_posix_mode,
    .dirent_iterator_init = fdio_default_dirent_iterator_init,
    .dirent_iterator_next = fdio_default_dirent_iterator_next,
    .dirent_iterator_destroy = fdio_default_dirent_iterator_destroy,
    .unlink = fdio_default_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_zxio_get_flags,
    .set_flags = fdio_zxio_set_flags,
    .bind = fdio_default_bind,
    .connect = fdio_default_connect,
    .listen = fdio_default_listen,
    .accept = fdio_default_accept,
    .getsockname = fdio_default_getsockname,
    .getpeername = fdio_default_getpeername,
    .getsockopt = fdio_default_getsockopt,
    .setsockopt = fdio_default_setsockopt,
    .recvmsg = fdio_zxio_recvmsg,
    .sendmsg = fdio_zxio_sendmsg,
    .shutdown = fdio_default_shutdown,
};

fdio_t* fdio_vmofile_create(fio::File::SyncClient control, zx::vmo vmo, zx_off_t offset,
                            zx_off_t length, zx_off_t seek) {
  fdio_t* io = fdio_alloc(&fdio_zxio_vmofile_ops);
  if (io == nullptr) {
    return nullptr;
  }
  zx_status_t status = zxio_vmofile_init(fdio_get_zxio_storage(io), std::move(control),
                                         std::move(vmo), offset, length, seek);
  if (status != ZX_OK) {
    return nullptr;
  }
  return io;
}

// Pipe ------------------------------------------------------------------------

static inline zxio_pipe_t* fdio_get_zxio_pipe(fdio_t* io) {
  return (zxio_pipe_t*)fdio_get_zxio(io);
}

zx_status_t fdio_zx_socket_posix_ioctl(const zx::socket& socket, int request, va_list va) {
  switch (request) {
    case FIONREAD: {
      zx_info_socket_t info;
      memset(&info, 0, sizeof(info));
      zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
      if (status != ZX_OK) {
        return status;
      }
      size_t available = info.rx_buf_available;
      if (available > INT_MAX) {
        available = INT_MAX;
      }
      int* actual = va_arg(va, int*);
      *actual = static_cast<int>(available);
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

static zx_status_t fdio_zxio_pipe_posix_ioctl(fdio_t* io, int request, va_list va) {
  return fdio_zx_socket_posix_ioctl(fdio_get_zxio_pipe(io)->socket, request, va);
}

zx_status_t fdio_zxio_recvmsg(fdio_t* io, struct msghdr* msg, int flags, size_t* out_actual,
                              int16_t* out_code) {
  zxio_flags_t zxio_flags = 0;
  if (flags & MSG_PEEK) {
    zxio_flags |= ZXIO_PEEK;
    flags &= ~MSG_PEEK;
  }
  if (flags) {
    // TODO: support MSG_OOB
    return ZX_ERR_NOT_SUPPORTED;
  }

  *out_code = 0;

  // Variable length arrays have to have nonzero sizes, so we can't allocate a zx_iov for an empty
  // io vector. Instead, we can ask to read zero entries with a null vector.
  if (msg->msg_iovlen == 0) {
    return zxio_readv(fdio_get_zxio(io), nullptr, 0, zxio_flags, out_actual);
  }

  zx_iovec_t zx_iov[msg->msg_iovlen];
  for (int i = 0; i < msg->msg_iovlen; ++i) {
    zx_iov[i] = {
        .buffer = msg->msg_iov[i].iov_base,
        .capacity = msg->msg_iov[i].iov_len,
    };
  }

  return zxio_readv(fdio_get_zxio(io), zx_iov, msg->msg_iovlen, zxio_flags, out_actual);
}

zx_status_t fdio_zxio_sendmsg(fdio_t* io, const struct msghdr* msg, int flags, size_t* out_actual,
                              int16_t* out_code) {
  if (flags) {
    // TODO: support MSG_NOSIGNAL
    // TODO: support MSG_OOB
    return ZX_ERR_NOT_SUPPORTED;
  }

  *out_code = 0;

  // Variable length arrays have to have nonzero sizes, so we can't allocate a zx_iov for an empty
  // io vector. Instead, we can ask to write zero entries with a null vector.
  if (msg->msg_iovlen == 0) {
    return zxio_writev(fdio_get_zxio(io), nullptr, 0, 0, out_actual);
  }

  zx_iovec_t zx_iov[msg->msg_iovlen];
  for (int i = 0; i < msg->msg_iovlen; ++i) {
    zx_iov[i] = {
        .buffer = msg->msg_iov[i].iov_base,
        .capacity = msg->msg_iov[i].iov_len,
    };
  }
  return zxio_writev(fdio_get_zxio(io), zx_iov, msg->msg_iovlen, 0, out_actual);
}

zx_status_t fdio_zx_socket_shutdown(const zx::socket& socket, int how) {
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

static zx_status_t fdio_zxio_pipe_shutdown(fdio_t* io, int how, int16_t* out_code) {
  *out_code = 0;

  return fdio_zx_socket_shutdown(fdio_get_zxio_pipe(io)->socket, how);
}

static constexpr fdio_ops_t fdio_zxio_pipe_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .borrow_channel = fdio_default_borrow_channel,
    .wait_begin = fdio_zxio_wait_begin,
    .wait_end = fdio_zxio_wait_end,
    .posix_ioctl = fdio_zxio_pipe_posix_ioctl,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .convert_to_posix_mode = fdio_default_convert_to_posix_mode,
    .dirent_iterator_init = fdio_default_dirent_iterator_init,
    .dirent_iterator_next = fdio_default_dirent_iterator_next,
    .dirent_iterator_destroy = fdio_default_dirent_iterator_destroy,
    .unlink = fdio_default_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .bind = fdio_default_bind,
    .connect = fdio_default_connect,
    .listen = fdio_default_listen,
    .accept = fdio_default_accept,
    .getsockname = fdio_default_getsockname,
    .getpeername = fdio_default_getpeername,
    .getsockopt = fdio_default_getsockopt,
    .setsockopt = fdio_default_setsockopt,
    .recvmsg = fdio_zxio_recvmsg,
    .sendmsg = fdio_zxio_sendmsg,
    .shutdown = fdio_zxio_pipe_shutdown,
};

fdio_t* fdio_pipe_create(zx::socket socket) {
  fdio_t* io = fdio_alloc(&fdio_zxio_pipe_ops);
  if (io == nullptr) {
    return nullptr;
  }
  zx_info_socket_t info;
  zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return nullptr;
  }
  status = zxio_pipe_init(fdio_get_zxio_storage(io), std::move(socket), info);
  if (status != ZX_OK) {
    return nullptr;
  }
  return io;
}

int fdio_pipe_pair(fdio_t** _a, fdio_t** _b, uint32_t options) {
  zx::socket h0, h1;
  fdio_t *a, *b;
  zx_status_t r;
  if ((r = zx::socket::create(options, &h0, &h1)) < 0) {
    return r;
  }
  if ((a = fdio_pipe_create(std::move(h0))) == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  if ((b = fdio_pipe_create(std::move(h1))) == nullptr) {
    fdio_zxio_close(a);
    return ZX_ERR_NO_MEMORY;
  }
  *_a = a;
  *_b = b;
  return 0;
}

__EXPORT
zx_status_t fdio_pipe_half(int* out_fd, zx_handle_t* out_handle) {
  zx::socket h0, h1;
  zx_status_t r;
  fdio_t* io;
  if ((r = zx::socket::create(0, &h0, &h1)) < 0) {
    return r;
  }
  if ((io = fdio_pipe_create(std::move(h0))) == nullptr) {
    r = ZX_ERR_NO_MEMORY;
  }
  if ((*out_fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
    fdio_release(io);
    r = ZX_ERR_NO_RESOURCES;
  }
  *out_handle = h1.release();
  return ZX_OK;

  return r;
}

// Debuglog --------------------------------------------------------------------

fdio_t* fdio_logger_create(zx::debuglog handle) {
  zxio_storage_t* storage = nullptr;
  fdio_t* io = fdio_zxio_create(&storage);
  if (io == nullptr) {
    return nullptr;
  }
  zx_status_t status = zxio_debuglog_init(storage, std::move(handle));
  ZX_ASSERT(status == ZX_OK);
  return io;
}
