// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fdio_unistd.h"

#include <fbl/auto_lock.h>

#include "internal.h"

std::optional<int> bind_to_fd(const fdio_ptr& io) {
  fbl::AutoLock lock(&fdio_lock);
  return bind_to_fd_locked(io);
}

std::optional<int> bind_to_fd_locked(const fdio_ptr& io) {
  for (size_t fd = 0; fd < fdio_fdtab.size(); ++fd) {
    if (fdio_fdtab[fd].try_set(io)) {
      return fd;
    }
  }
  return std::nullopt;
}

namespace {

fdio_slot* slot_locked(int fd) __TA_REQUIRES(fdio_lock) {
  if ((fd < 0) || (fd >= FDIO_MAX_FD)) {
    return nullptr;
  }
  return &fdio_fdtab[fd];
}

}  // namespace

fdio_ptr fd_to_io(int fd) {
  fbl::AutoLock lock(&fdio_lock);
  return fd_to_io_locked(fd);
}

fdio_ptr fd_to_io_locked(int fd) {
  fdio_slot* slot = slot_locked(fd);
  if (slot == nullptr) {
    return nullptr;
  }
  return slot->get();
}

fdio_ptr unbind_from_fd(int fd) {
  fbl::AutoLock lock(&fdio_lock);
  return unbind_from_fd_locked(fd);
}

fdio_ptr unbind_from_fd_locked(int fd) __TA_REQUIRES(fdio_lock) {
  fdio_slot* slot = slot_locked(fd);
  if (slot == nullptr) {
    return nullptr;
  }
  return slot->release();
}

// TODO(fxbug.dev/30921): determine complete correct mapping
int fdio_status_to_errno(zx_status_t status) {
  switch (status) {
    case ZX_ERR_NOT_FOUND:
      return ENOENT;
    case ZX_ERR_NO_MEMORY:
      return ENOMEM;
    case ZX_ERR_INVALID_ARGS:
      return EINVAL;
    case ZX_ERR_BUFFER_TOO_SMALL:
      return EINVAL;
    case ZX_ERR_TIMED_OUT:
      return ETIMEDOUT;
    case ZX_ERR_UNAVAILABLE:
      return EBUSY;
    case ZX_ERR_ALREADY_EXISTS:
      return EEXIST;
    case ZX_ERR_PEER_CLOSED:
      return EPIPE;
    case ZX_ERR_BAD_STATE:
      return EPIPE;
    case ZX_ERR_BAD_PATH:
      return ENAMETOOLONG;
    case ZX_ERR_IO:
      return EIO;
    case ZX_ERR_NOT_FILE:
      return EISDIR;
    case ZX_ERR_NOT_DIR:
      return ENOTDIR;
    case ZX_ERR_NOT_SUPPORTED:
      return ENOTSUP;
    case ZX_ERR_WRONG_TYPE:
      return ENOTSUP;
    case ZX_ERR_OUT_OF_RANGE:
      return EINVAL;
    case ZX_ERR_NO_RESOURCES:
      return ENOMEM;
    case ZX_ERR_BAD_HANDLE:
      return EBADF;
    case ZX_ERR_ACCESS_DENIED:
      return EACCES;
    case ZX_ERR_SHOULD_WAIT:
      return EAGAIN;
    case ZX_ERR_FILE_BIG:
      return EFBIG;
    case ZX_ERR_NO_SPACE:
      return ENOSPC;
    case ZX_ERR_NOT_EMPTY:
      return ENOTEMPTY;
    case ZX_ERR_IO_REFUSED:
      return ECONNREFUSED;
    case ZX_ERR_IO_INVALID:
      return EIO;
    case ZX_ERR_CANCELED:
      return EBADF;
    case ZX_ERR_PROTOCOL_NOT_SUPPORTED:
      return EPROTONOSUPPORT;
    case ZX_ERR_ADDRESS_UNREACHABLE:
      return ENETUNREACH;
    case ZX_ERR_ADDRESS_IN_USE:
      return EADDRINUSE;
    case ZX_ERR_NOT_CONNECTED:
      return ENOTCONN;
    case ZX_ERR_CONNECTION_REFUSED:
      return ECONNREFUSED;
    case ZX_ERR_CONNECTION_RESET:
      return ECONNRESET;
    case ZX_ERR_CONNECTION_ABORTED:
      return ECONNABORTED;

    // No specific translation, so return a generic value.
    default:
      return EIO;
  }
}
