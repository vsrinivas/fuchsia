// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_FDIO_UNISTD_H_
#define LIB_FDIO_FDIO_UNISTD_H_

#include <zircon/compiler.h>

#include <cerrno>

#include "internal.h"

std::optional<int> bind_to_fd_locked(const fdio_ptr& io) __TA_REQUIRES(fdio_lock);
std::optional<int> bind_to_fd(const fdio_ptr& io) __TA_EXCLUDES(fdio_lock);
fdio_ptr fd_to_io_locked(int fd) __TA_REQUIRES(fdio_lock);
fdio_ptr fd_to_io(int fd) __TA_EXCLUDES(fdio_lock);
fdio_ptr unbind_from_fd_locked(int fd) __TA_REQUIRES(fdio_lock);
fdio_ptr unbind_from_fd(int fd) __TA_EXCLUDES(fdio_lock);

int fdio_status_to_errno(zx_status_t status);

// Sets |errno| to the nearest match for |status| and returns -1;
static inline int ERROR(zx_status_t status) {
  errno = fdio_status_to_errno(status);
  return -1;
}

// Returns 0 if |status| is |ZX_OK|, otherwise delegates to |ERROR|.
static inline int STATUS(zx_status_t status) {
  if (status == ZX_OK) {
    return 0;
  }
  return ERROR(status);
}

// set errno to e, return -1
static inline int ERRNO(int e) {
  errno = e;
  return -1;
}

#endif  // LIB_FDIO_FDIO_UNISTD_H_
