// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zxio.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zxio/bsdsocket.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/null.h>
#include <lib/zxio/watcher.h>
#include <lib/zxio/zxio.h>
#include <poll.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <zircon/rights.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fbl/auto_lock.h>

#include "fdio_unistd.h"

namespace fio = fuchsia_io;

namespace fdio_internal {

zx::status<fdio_ptr> zxio::create() {
  fdio_ptr io = fbl::MakeRefCounted<zxio>();
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zxio_default_init(&io->zxio_storage().io);
  return zx::ok(io);
}

zx::status<fdio_ptr> zxio::create_null() {
  fdio_ptr io = fbl::MakeRefCounted<zxio>();
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zxio_null_init(&io->zxio_storage().io);
  return zx::ok(io);
}

zx_status_t zxio::close() { return zxio_close(&zxio_storage().io); }

zx_status_t zxio::borrow_channel(zx_handle_t* out_borrowed) {
  return zxio_borrow(&zxio_storage().io, out_borrowed);
}

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

Errno zxio::posix_ioctl(int request, va_list va) {
  switch (request) {
    case TIOCGWINSZ: {
      uint32_t width, height;
      zx_status_t status = zxio_get_window_size(&zxio_storage().io, &width, &height);
      if (status != ZX_OK) {
        return Errno(ENOTTY);
      }
      struct winsize size = {
          .ws_row = static_cast<uint16_t>(height),
          .ws_col = static_cast<uint16_t>(width),
      };
      struct winsize* out_size = va_arg(va, struct winsize*);
      *out_size = size;
      return Errno(Errno::Ok);
    }
    case TIOCSWINSZ: {
      const struct winsize* in_size = va_arg(va, const struct winsize*);
      zx_status_t status =
          zxio_set_window_size(&zxio_storage().io, in_size->ws_col, in_size->ws_row);
      if (status != ZX_OK) {
        return Errno(ENOTTY);
      }
      return Errno(Errno::Ok);
    }
    case FIONREAD: {
      size_t available = 0u;
      zx_status_t status = zxio_get_read_buffer_available(&zxio_storage().io, &available);
      if (status != ZX_OK) {
        return Errno(ENOTTY);
      }
      if (available > INT_MAX) {
        available = INT_MAX;
      }
      int* actual = va_arg(va, int*);
      *actual = static_cast<int>(available);
      return Errno(Errno::Ok);
    }
    default:
      int16_t out_code;
      zx_status_t status = zxio_ioctl(&zxio_storage().io, request, &out_code, va);
      if (status != ZX_OK) {
        return Errno(ENOTTY);
      }
      return Errno(out_code);
  }
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
                                       zxio_dirent_t* inout_entry) {
  return zxio_dirent_iterator_next(iterator, inout_entry);
}

void zxio::dirent_iterator_destroy(zxio_dirent_iterator_t* iterator) {
  return zxio_dirent_iterator_destroy(iterator);
}

zx_status_t zxio::watch_directory(zxio_watch_directory_cb cb, zx_time_t deadline, void* context) {
  return zxio_watch_directory(&zxio_storage().io, cb, deadline, context);
}

zx_status_t zxio::unlink(std::string_view name, int flags) {
  return zxio_unlink(&zxio_storage().io, name.data(), name.length(), flags);
}

zx_status_t zxio::truncate(uint64_t off) { return zxio_truncate(&zxio_storage().io, off); }

zx_status_t zxio::rename(std::string_view src, zx_handle_t dst_token, std::string_view dst) {
  return zxio_rename(&zxio_storage().io, src.data(), src.length(), dst_token, dst.data(),
                     dst.length());
}

zx_status_t zxio::link(std::string_view src, zx_handle_t dst_token, std::string_view dst) {
  return zxio_link(&zxio_storage().io, src.data(), src.length(), dst_token, dst.data(),
                   dst.length());
}

zx_status_t zxio::get_flags(fio::wire::OpenFlags* out_flags) {
  return zxio_flags_get(&zxio_storage().io, reinterpret_cast<uint32_t*>(out_flags));
}

zx_status_t zxio::set_flags(fio::wire::OpenFlags flags) {
  return zxio_flags_set(&zxio_storage().io, static_cast<uint32_t>(flags));
}

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
    iovec const& iov = msg->msg_iov[i];
    zx_iov[i] = {
        .buffer = iov.iov_base,
        .capacity = iov.iov_len,
    };
  }

  return zxio_readv(&zxio_storage().io, zx_iov, msg->msg_iovlen, zxio_flags, out_actual);
}

zx_status_t zxio::sendmsg_inner(const struct msghdr* msg, int flags, size_t* out_actual) {
  if (flags) {
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

zx::status<fdio_ptr> pipe::create(zx::socket socket) {
  fdio_ptr io = fbl::MakeRefCounted<pipe>();
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zx_info_socket_t info;
  zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  status = ::zxio::CreatePipe(&io->zxio_storage(), std::move(socket), info);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(io);
}

zx::status<std::pair<fdio_ptr, fdio_ptr>> pipe::create_pair(uint32_t options) {
  zx::socket h0, h1;
  zx_status_t status = zx::socket::create(options, &h0, &h1);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  zx::status a = pipe::create(std::move(h0));
  if (a.is_error()) {
    return a.take_error();
  }
  zx::status b = pipe::create(std::move(h1));
  if (b.is_error()) {
    return b.take_error();
  }
  return zx::ok(std::make_pair(a.value(), b.value()));
}

zx_status_t pipe::recvmsg(struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
  *out_code = 0;
  zx_status_t status = recvmsg_inner(msg, flags, out_actual);

  // We've reached end-of-file, which is signaled by successfully reading zero
  // bytes.
  //
  // If we see |ZX_ERR_BAD_STATE|, that implies reading has been disabled for
  // this endpoint.
  if (status == ZX_ERR_PEER_CLOSED || status == ZX_ERR_BAD_STATE) {
    *out_actual = 0;
    status = ZX_OK;
  }
  return status;
}

zx::status<fdio_ptr> open_async(zxio_t* directory, std::string_view path,
                                fio::wire::OpenFlags flags, uint32_t mode) {
  zx::status endpoints = fidl::CreateEndpoints<fio::Node>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  zx_status_t status = zxio_open_async(directory, static_cast<uint32_t>(flags), mode, path.data(),
                                       path.length(), endpoints->server.channel().release());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  if (flags & fio::wire::OpenFlags::kDescribe) {
    return fdio::create_with_on_open(std::move(endpoints->client));
  }

  return remote::create(std::move(endpoints->client));
}

zx::status<fdio_ptr> remote::open(std::string_view path, fio::wire::OpenFlags flags,
                                  uint32_t mode) {
  return open_async(&zxio_storage().io, path, flags, mode);
}

void remote::wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) {
  // POLLERR is always detected.
  events |= POLLERR;

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
  zxio_wait_begin(&zxio_storage().io, signals, handle, out_signals);
}

void remote::wait_end(zx_signals_t signals, uint32_t* out_events) {
  zxio_signals_t zxio_signals = 0;
  zxio_wait_end(&zxio_storage().io, signals, &zxio_signals);

  uint32_t events = 0;
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    events |= POLLIN;
  }
  if (zxio_signals & ZXIO_SIGNAL_OUT_OF_BAND) {
    events |= POLLPRI;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    events |= POLLOUT;
  }
  if (zxio_signals & ZXIO_SIGNAL_ERROR) {
    events |= POLLERR;
  }
  if (zxio_signals & ZXIO_SIGNAL_PEER_CLOSED) {
    events |= POLLHUP;
  }
  if (zxio_signals & ZXIO_SIGNAL_READ_DISABLED) {
    events |= POLLRDHUP;
  }
  *out_events = events;
}

zx::status<fdio_ptr> remote::create(fidl::ClientEnd<fuchsia_io::Node> node) {
  fdio_ptr io = fbl::MakeRefCounted<remote>();
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zx_status_t status = ::zxio::CreateNode(&io->zxio_storage(), std::move(node));
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(io);
}

zx::status<fdio_ptr> remote::create(zx::vmo vmo, zx::stream stream) {
  fdio_ptr io = fbl::MakeRefCounted<remote>();
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zx_status_t status = ::zxio::CreateVmo(&io->zxio_storage(), std::move(vmo), std::move(stream));
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(io);
}

}  // namespace fdio_internal
