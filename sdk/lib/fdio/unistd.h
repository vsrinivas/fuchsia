// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_UNISTD_H_
#define LIB_FDIO_UNISTD_H_

#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <sys/types.h>
#include <threads.h>

#include <cerrno>

#include "internal.h"

// fd_to_io calls fdio_acquire on the fd, must call fdio_release() when done.
#define fd_to_io(n) fdio_unsafe_fd_to_io(n)

int fdio_status_to_errno(zx_status_t status);

// set errno to the closest match for error and return -1
static inline int ERROR(zx_status_t error) {
  errno = fdio_status_to_errno(error);
  return -1;
}

// if status is negative, set errno as appropriate and return -1
// otherwise return status
static inline int STATUS(zx_status_t status) {
  if (status < 0) {
    errno = fdio_status_to_errno(status);
    return -1;
  } else {
    return status;
  }
}

// set errno to e, return -1
static inline int ERRNO(int e) {
  errno = e;
  return -1;
}

#endif  // LIB_FDIO_UNISTD_H_
