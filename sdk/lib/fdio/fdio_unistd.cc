// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fdio_unistd.h"

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
