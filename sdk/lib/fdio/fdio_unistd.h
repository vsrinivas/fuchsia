// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_FDIO_UNISTD_H_
#define LIB_FDIO_FDIO_UNISTD_H_

#include <lib/fdio/limits.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cerrno>
#include <optional>

#include <fbl/ref_ptr.h>

#include "sdk/lib/fdio/fdio_state.h"

struct fdio;

std::optional<int> bind_to_fd_locked(const fbl::RefPtr<fdio>& io) __TA_REQUIRES(fdio_lock);
std::optional<int> bind_to_fd(const fbl::RefPtr<fdio>& io) __TA_EXCLUDES(fdio_lock);
fbl::RefPtr<fdio> fd_to_io_locked(int fd) __TA_REQUIRES(fdio_lock);
fbl::RefPtr<fdio> fd_to_io(int fd) __TA_EXCLUDES(fdio_lock);
fbl::RefPtr<fdio> unbind_from_fd_locked(int fd) __TA_REQUIRES(fdio_lock);
fbl::RefPtr<fdio> unbind_from_fd(int fd) __TA_EXCLUDES(fdio_lock);

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
