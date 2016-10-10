// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxio/io.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/types.h>
#include <threads.h>

#include "private.h"

typedef struct {
    mtx_t lock;
    mtx_t cwd_lock;
    bool init;
    mode_t umask;
    mxio_t* root;
    mxio_t* cwd;
    mxio_t* fdtab[MAX_MXIO_FD];
    char cwd_path[PATH_MAX];
} mxio_state_t;

extern mxio_state_t __mxio_global_state;

#define mxio_lock (__mxio_global_state.lock)
#define mxio_root_handle (__mxio_global_state.root)
#define mxio_cwd_handle (__mxio_global_state.cwd)
#define mxio_cwd_lock (__mxio_global_state.cwd_lock)
#define mxio_cwd_path (__mxio_global_state.cwd_path)
#define mxio_fdtab (__mxio_global_state.fdtab)
#define mxio_root_init (__mxio_global_state.init)

mxio_t* __mxio_fd_to_io(int fd);

#define fd_to_io(n) __mxio_fd_to_io(n)

mx_status_t __mxio_open(mxio_t** io, const char* path, int flags, uint32_t mode);

int mxio_status_to_errno(mx_status_t status);

// set errno to the closest match for error and return -1
static inline int ERROR(mx_status_t error) {
    errno = mxio_status_to_errno(error);
    return -1;
}

// if status is negative, set errno as appropriate and return -1
// otherwise return status
static inline int STATUS(mx_status_t status) {
    if (status < 0) {
        errno = mxio_status_to_errno(status);
        return -1;
    } else {
        return status;
    }
}

// set errno to e, return -1
static inline inline int ERRNO(int e) {
    errno = e;
    return -1;
}
