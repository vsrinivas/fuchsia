// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zxio.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zxio/bsdsocket.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/cpp/transitional.h>
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

zx::result<fdio_ptr> zxio::create() {
  fdio_ptr io = fbl::MakeRefCounted<zxio>();
  if (io == nullptr) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  zxio_default_init(&io->zxio_storage().io);
  return zx::ok(io);
}

zx::result<fdio_ptr> zxio::create_null() {
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
  return zxio_wait_begin_inner(&zxio_storage().io, events, ZXIO_SIGNAL_NONE, out_handle,
                               out_signals);
}

void zxio::wait_end(zx_signals_t signals, uint32_t* out_events) {
  return zxio_wait_end_inner(&zxio_storage().io, signals, out_events, nullptr);
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

zx_status_t zxio::recvmsg(struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
  *out_code = 0;
  return zxio_recvmsg_inner(&zxio_storage().io, msg, flags, out_actual);
}

zx_status_t zxio::sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                          int16_t* out_code) {
  *out_code = 0;
  return zxio_sendmsg_inner(&zxio_storage().io, msg, flags, out_actual);
}

zx::result<fdio_ptr> pipe::create(zx::socket socket) {
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

zx::result<std::pair<fdio_ptr, fdio_ptr>> pipe::create_pair(uint32_t options) {
  zx::socket h0, h1;
  zx_status_t status = zx::socket::create(options, &h0, &h1);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  zx::result a = pipe::create(std::move(h0));
  if (a.is_error()) {
    return a.take_error();
  }
  zx::result b = pipe::create(std::move(h1));
  if (b.is_error()) {
    return b.take_error();
  }
  return zx::ok(std::make_pair(a.value(), b.value()));
}

zx_status_t pipe::recvmsg(struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
  *out_code = 0;
  zx_status_t status = zxio_recvmsg_inner(&zxio_storage().io, msg, flags, out_actual);

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

zx::result<fdio_ptr> open_async(zxio_t* directory, std::string_view path,
                                fio::wire::OpenFlags flags, uint32_t mode) {
  zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
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

zx::result<fdio_ptr> remote::open(std::string_view path, fio::wire::OpenFlags flags,
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

zx::result<fdio_ptr> remote::create(fidl::ClientEnd<fuchsia_io::Node> node) {
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

zx::result<fdio_ptr> remote::create(zx::vmo vmo, zx::stream stream) {
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
