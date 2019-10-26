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

static void fdio_zxio_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* out_handle,
                                 zx_signals_t* out_signals) {
  zxio_t* z = fdio_get_zxio(io);
  zxio_signals_t signals = ZXIO_SIGNAL_NONE;
  if (events & POLLIN) {
    signals |= ZXIO_READABLE | ZXIO_READ_DISABLED;
  }
  if (events & POLLOUT) {
    signals |= ZXIO_WRITABLE | ZXIO_WRITE_DISABLED;
  }
  if (events & POLLRDHUP) {
    signals |= ZXIO_READ_DISABLED;
  }
  zxio_wait_begin(z, signals, out_handle, out_signals);
}

static void fdio_zxio_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* out_events) {
  zxio_t* z = fdio_get_zxio(io);
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  zxio_wait_end(z, signals, &zxio_signals);

  uint32_t events = 0;
  if (zxio_signals & (ZXIO_READABLE | ZXIO_READ_DISABLED)) {
    events |= POLLIN;
  }
  if (zxio_signals & (ZXIO_WRITABLE | ZXIO_WRITE_DISABLED)) {
    events |= POLLOUT;
  }
  if (zxio_signals & ZXIO_READ_DISABLED) {
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

static zx_status_t fdio_zxio_sync(fdio_t* io) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_sync(z);
}

static zx_status_t fdio_zxio_get_attr(fdio_t* io, fio::NodeAttributes* out) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_attr_get(z, out);
}

static zx_status_t fdio_zxio_set_attr(fdio_t* io, uint32_t flags, const fio::NodeAttributes* attr) {
  zxio_t* z = fdio_get_zxio(io);
  return zxio_attr_set(z, flags, attr);
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

static zx_status_t fdio_zxio_get_vmo(fdio_t* io, int flags, zx::vmo* out_vmo) {
  zxio_t* z = fdio_get_zxio(io);
  zx::vmo vmo;
  size_t vmo_size;
  zx_status_t status = zxio_vmo_get(z, flags, vmo.reset_and_get_address(), &vmo_size);
  if (status != ZX_OK) {
    return status;
  }
  *out_vmo = std::move(vmo);
  return ZX_OK;
}

// Generic ---------------------------------------------------------------------

static fdio_ops_t fdio_zxio_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = fdio_zxio_wait_begin,
    .wait_end = fdio_zxio_wait_end,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_zxio_get_flags,
    .set_flags = fdio_zxio_set_flags,
    .recvmsg = fdio_zxio_recvmsg,
    .sendmsg = fdio_zxio_sendmsg,
    .shutdown = fdio_default_shutdown,
};

__EXPORT
fdio_t* fdio_zxio_create(zxio_storage_t** out_storage) {
  fdio_t* io = fdio_alloc(&fdio_zxio_ops);
  if (io == NULL) {
    return NULL;
  }
  zxio_null_init(&fdio_get_zxio_storage(io)->io);
  *out_storage = fdio_get_zxio_storage(io);
  return io;
}

// Null ------------------------------------------------------------------------

__EXPORT
fdio_t* fdio_null_create(void) {
  zxio_storage_t* storage = NULL;
  return fdio_zxio_create(&storage);
}

// Remote ----------------------------------------------------------------------

// POLL_MASK and POLL_SHIFT intend to convert the lower five POLL events into
// ZX_USER_SIGNALs and vice-versa. Other events need to be manually converted to
// a zx_signals_t, if they are desired.
#define POLL_SHIFT 24
#define POLL_MASK 0x1F

static zxio_remote_t* fdio_get_zxio_remote(fdio_t* io) { return (zxio_remote_t*)fdio_get_zxio(io); }

static void fdio_zxio_remote_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle,
                                        zx_signals_t* _signals) {
  zxio_remote_t* rio = fdio_get_zxio_remote(io);
  *handle = rio->event;

  zx_signals_t signals = 0;
  // Manually add signals that don't fit within POLL_MASK
  if (events & POLLRDHUP) {
    signals |= ZX_CHANNEL_PEER_CLOSED;
  }

  // POLLERR is always detected
  *_signals = (((POLLERR | events) & POLL_MASK) << POLL_SHIFT) | signals;
}

static void fdio_zxio_remote_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
  // Manually add events that don't fit within POLL_MASK
  uint32_t events = 0;
  if (signals & ZX_CHANNEL_PEER_CLOSED) {
    events |= POLLRDHUP;
  }
  *_events = ((signals >> POLL_SHIFT) & POLL_MASK) | events;
}

static zx_status_t fdio_zxio_remote_readdir(fdio_t* io, void* ptr, size_t max, size_t* out_actual) {
  zxio_remote_t* rio = fdio_get_zxio_remote(io);
  uint8_t request_buffer[fidl::MaxSizeInChannel<fio::Directory::ReadDirentsRequest>()];
  fidl::DecodedMessage<fio::Directory::ReadDirentsRequest> request(
      fidl::BytePart::WrapFull(request_buffer));
  uint8_t response_buffer[fidl::MaxSizeInChannel<fio::Directory::ReadDirentsResponse>()];
  request.message()->max_bytes = max;
  fidl::DecodeResult result =
      fio::Directory::InPlace::ReadDirents(zx::unowned_channel(rio->control), std::move(request),
                                           fidl::BytePart::WrapEmpty(response_buffer));
  zx_status_t status = result.status;
  if (status != ZX_OK) {
    return status;
  }
  fio::Directory::ReadDirentsResponse* response = result.Unwrap();
  status = response->s;
  if (status != ZX_OK) {
    return status;
  }
  fidl::VectorView<uint8_t> dirents = response->dirents;
  if (dirents.count() > max) {
    return ZX_ERR_IO;
  }
  *out_actual = dirents.count();
  memcpy(ptr, dirents.data(), dirents.count());
  return ZX_OK;
}

static zx_status_t fdio_zxio_remote_rewind(fdio_t* io) {
  zxio_remote_t* rio = fdio_get_zxio_remote(io);
  auto result = fio::Directory::Call::Rewind(zx::unowned_channel(rio->control));
  return result.ok() ? result.Unwrap()->s : result.status();
}

static zx_status_t fdio_zxio_remote_unlink(fdio_t* io, const char* path, size_t len) {
  zxio_remote_t* rio = fdio_get_zxio_remote(io);
  auto result =
      fio::Directory::Call::Unlink(zx::unowned_channel(rio->control), fidl::StringView(path, len));
  return result.ok() ? result.Unwrap()->s : result.status();
}

static zx_status_t fdio_zxio_remote_link(fdio_t* io, const char* src, size_t srclen,
                                         zx_handle_t dst_token, const char* dst, size_t dstlen) {
  zxio_remote_t* rio = fdio_get_zxio_remote(io);
  auto result =
      fio::Directory::Call::Link(zx::unowned_channel(rio->control), fidl::StringView(src, srclen),
                                 zx::handle(dst_token), fidl::StringView(dst, dstlen));
  return result.ok() ? result.Unwrap()->s : result.status();
}

static fdio_ops_t fdio_zxio_remote_ops = {
    .close = fdio_zxio_close,
    .open = fdio_zxio_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = fdio_zxio_remote_wait_begin,
    .wait_end = fdio_zxio_remote_wait_end,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_zxio_get_vmo,
    .get_token = fdio_zxio_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .readdir = fdio_zxio_remote_readdir,
    .rewind = fdio_zxio_remote_rewind,
    .unlink = fdio_zxio_remote_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_zxio_rename,
    .link = fdio_zxio_remote_link,
    .get_flags = fdio_zxio_get_flags,
    .set_flags = fdio_zxio_set_flags,
    .recvmsg = fdio_zxio_recvmsg,
    .sendmsg = fdio_zxio_sendmsg,
    .shutdown = fdio_default_shutdown,
};

fdio_t* fdio_remote_create(zx_handle_t control, zx_handle_t event) {
  fdio_t* io = fdio_alloc(&fdio_zxio_remote_ops);
  if (io == NULL) {
    zx_handle_close(control);
    zx_handle_close(event);
    return NULL;
  }
  zx_status_t status = zxio_remote_init(fdio_get_zxio_storage(io), control, event);
  if (status != ZX_OK) {
    return NULL;
  }
  return io;
}

fdio_t* fdio_dir_create(zx_handle_t control) {
  fdio_t* io = fdio_alloc(&fdio_zxio_remote_ops);
  if (io == NULL) {
    zx_handle_close(control);
    return NULL;
  }
  zx_status_t status = zxio_dir_init(fdio_get_zxio_storage(io), control);
  if (status != ZX_OK) {
    return NULL;
  }
  return io;
}

fdio_t* fdio_file_create(zx_handle_t control, zx_handle_t event) {
  fdio_t* io = fdio_alloc(&fdio_zxio_remote_ops);
  if (io == NULL) {
    zx_handle_close(control);
    return NULL;
  }
  zx_status_t status = zxio_file_init(fdio_get_zxio_storage(io), control, event);
  if (status != ZX_OK) {
    return NULL;
  }
  return io;
}

__EXPORT
zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out) {
  mtx_lock(&fdio_lock);
  if ((fd < 0) || (fd >= FDIO_MAX_FD) || (fdio_fdtab[fd] == NULL)) {
    mtx_unlock(&fdio_lock);
    return ZX_ERR_NOT_FOUND;
  }
  fdio_t* io = fdio_fdtab[fd];
  fdio_dupcount_release(io);
  fdio_fdtab[fd] = NULL;
  if (fdio_get_dupcount(io) > 0) {
    // still alive in other fdtab slots
    // this fd goes away but we can't give away the handle
    mtx_unlock(&fdio_lock);
    fdio_release(io);
    return ZX_ERR_UNAVAILABLE;
  } else {
    mtx_unlock(&fdio_lock);
    zx_status_t r;
    if (fdio_get_ops(io) == &fdio_zxio_remote_ops) {
      zxio_remote_t* file = fdio_get_zxio_remote(io);
      r = zxio_release(&file->io, out);
    } else {
      r = ZX_ERR_NOT_SUPPORTED;
      fdio_get_ops(io)->close(io);
    }
    fdio_release(io);
    return r;
  }
}

__EXPORT
zx_handle_t fdio_unsafe_borrow_channel(fdio_t* io) {
  if (io == NULL) {
    return ZX_HANDLE_INVALID;
  }

  if (fdio_get_ops(io) == &fdio_zxio_remote_ops) {
    zxio_remote_t* file = fdio_get_zxio_remote(io);
    return file->control;
  }
  return ZX_HANDLE_INVALID;
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

static inline zxio_vmofile_t* fdio_get_zxio_vmofile(fdio_t* io) {
  return (zxio_vmofile_t*)fdio_get_zxio(io);
}

static zx_status_t fdio_zxio_vmofile_get_vmo(fdio_t* io, int flags, zx::vmo* out_vmo) {
  if (out_vmo == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }

  // fdio can't support Vmofiles with a non-zero start/offset, because it returns just a VMO with no
  // other data - like a starting offset - to the user. (Technically we could support any page
  // aligned offset, but that's currently unneeded.)
  zxio_vmofile_t* file = fdio_get_zxio_vmofile(io);
  if (file->start != 0) {
    return ZX_ERR_NOT_FOUND;
  }

  // Ensure that we return a VMO handle with only the rights requested by the client. For Vmofiles,
  // the server side does not ever see the VMO_FLAG_* options from the client because the VMO is
  // returned in NodeInfo/Vmofile rather than from a File.GetBuffer call.
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY;
  rights |= (flags & fio::VMO_FLAG_READ) ? ZX_RIGHT_READ : 0;
  rights |= (flags & fio::VMO_FLAG_WRITE) ? ZX_RIGHT_WRITE : 0;
  rights |= (flags & fio::VMO_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;

  if (flags & fio::VMO_FLAG_PRIVATE) {
    // Allow SET_PROPERTY only if creating a private child VMO so that the user can set ZX_PROP_NAME
    // (or similar).
    rights |= ZX_RIGHT_SET_PROPERTY;

    uint32_t options = ZX_VMO_CHILD_COPY_ON_WRITE;
    if (flags & fio::VMO_FLAG_EXEC) {
      // Creating a COPY_ON_WRITE child removes ZX_RIGHT_EXECUTE even if the parent VMO has it, and
      // we can't arbitrary add EXECUTE here on the client side. Adding CHILD_NO_WRITE still
      // creates a snapshot and a new VMO object, which e.g. can have a unique ZX_PROP_NAME value,
      // but the returned handle lacks WRITE and maintains EXECUTE.
      if (flags & fio::VMO_FLAG_WRITE) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      options |= ZX_VMO_CHILD_NO_WRITE;
    }

    zx::vmo child_vmo;
    zx_status_t status =
        file->vmo.vmo.create_child(options, file->start, file->vmo.size, &child_vmo);
    if (status != ZX_OK) {
      return status;
    }

    // COPY_ON_WRITE adds ZX_RIGHT_WRITE automatically, but we shouldn't return a handle with that
    // right unless requested using VMO_FLAG_WRITE.
    // TODO(fxb/36877): Supporting VMO_FLAG_PRIVATE & VMO_FLAG_WRITE for Vmofiles is a bit weird and
    // inconsistent. See bug for more info.
    return child_vmo.replace(rights, out_vmo);
  }

  // For !VMO_FLAG_PRIVATE (including VMO_FLAG_EXACT), we just duplicate another handle to the
  // Vmofile's VMO with appropriately scoped rights.
  return file->vmo.vmo.duplicate(rights, out_vmo);
}

static fdio_ops_t fdio_zxio_vmofile_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = fdio_default_wait_begin,
    .wait_end = fdio_default_wait_end,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_zxio_vmofile_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_zxio_get_flags,
    .set_flags = fdio_zxio_set_flags,
    .recvmsg = fdio_zxio_recvmsg,
    .sendmsg = fdio_zxio_sendmsg,
    .shutdown = fdio_default_shutdown,
};

fdio_t* fdio_vmofile_create(fio::File::SyncClient control, zx::vmo vmo, zx_off_t offset,
                            zx_off_t length, zx_off_t seek) {
  fdio_t* io = fdio_alloc(&fdio_zxio_vmofile_ops);
  if (io == NULL) {
    return NULL;
  }
  zx_status_t status = zxio_vmofile_init(fdio_get_zxio_storage(io), std::move(control),
                                         std::move(vmo), offset, length, seek);
  if (status != ZX_OK) {
    return NULL;
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
      zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL);
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

zx_status_t fdio_zxio_recvmsg(fdio_t* io, struct msghdr* msg, int flags, size_t* out_actual) {
  zxio_flags_t zxio_flags = 0;
  if (flags & MSG_PEEK) {
    zxio_flags |= ZXIO_PEEK;
    flags &= ~MSG_PEEK;
  }
  if (flags) {
    // TODO: support MSG_OOB
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_iovec_t zx_iov[msg->msg_iovlen];
  for (int i = 0; i < msg->msg_iovlen; ++i) {
    zx_iov[i] = {
        .buffer = msg->msg_iov[i].iov_base,
        .capacity = msg->msg_iov[i].iov_len,
    };
  }
  return zxio_read_vector(fdio_get_zxio(io), zx_iov, msg->msg_iovlen, zxio_flags, out_actual);
}

zx_status_t fdio_zxio_sendmsg(fdio_t* io, const struct msghdr* msg, int flags, size_t* out_actual) {
  if (flags) {
    // TODO: support MSG_NOSIGNAL
    // TODO: support MSG_OOB
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_iovec_t zx_iov[msg->msg_iovlen];
  for (int i = 0; i < msg->msg_iovlen; ++i) {
    zx_iov[i] = {
        .buffer = msg->msg_iov[i].iov_base,
        .capacity = msg->msg_iov[i].iov_len,
    };
  }
  return zxio_write_vector(fdio_get_zxio(io), zx_iov, msg->msg_iovlen, 0, out_actual);
}

zx_status_t fdio_zx_socket_shutdown(const zx::socket& socket, int how) {
  uint32_t options = 0;
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

static zx_status_t fdio_zxio_pipe_shutdown(fdio_t* io, int how) {
  return fdio_zx_socket_shutdown(fdio_get_zxio_pipe(io)->socket, how);
}

static fdio_ops_t fdio_zxio_pipe_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = fdio_zxio_wait_begin,
    .wait_end = fdio_zxio_wait_end,
    .posix_ioctl = fdio_zxio_pipe_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
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

int fdio_pipe_pair(fdio_t** _a, fdio_t** _b) {
  zx::socket h0, h1;
  fdio_t *a, *b;
  zx_status_t r;
  if ((r = zx::socket::create(0, &h0, &h1)) < 0) {
    return r;
  }
  if ((a = fdio_pipe_create(std::move(h0))) == NULL) {
    return ZX_ERR_NO_MEMORY;
  }
  if ((b = fdio_pipe_create(std::move(h1))) == NULL) {
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
  if ((io = fdio_pipe_create(std::move(h0))) == NULL) {
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
  zxio_storage_t* storage = NULL;
  fdio_t* io = fdio_zxio_create(&storage);
  if (io == NULL) {
    return NULL;
  }
  zx_status_t status = zxio_debuglog_init(storage, std::move(handle));
  ZX_ASSERT(status == ZX_OK);
  return io;
}
