// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <errno.h>
#include <fdio/io.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/types.h>
#include <threads.h>

#include "private.h"

fdio_t* __fdio_fd_to_io(int fd);

#define fd_to_io(n) __fdio_fd_to_io(n)

zx_status_t __fdio_open_at(fdio_t** io, int dirfd, const char* path, int flags, uint32_t mode);
zx_status_t __fdio_open(fdio_t** io, const char* path, int flags, uint32_t mode);

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
