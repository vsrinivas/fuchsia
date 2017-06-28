// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/socket.h>

#include <magenta/syscalls.h>

#include <errno.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include "private.h"
#include "unistd.h"

static int checksocket(int fd, int sock_err, int err) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        errno = EBADF;
        return -1;
    }
    int32_t is_socket = io->flags & MXIO_FLAG_SOCKET;
    mxio_release(io);
    if (!is_socket) {
        errno = sock_err;
        return -1;
    }
    if (err) {
        errno = err;
        return -1;
    }
    return 0;
}

int socketpair(int domain, int type, int protocol, int fd[2]) {
    if (type != SOCK_STREAM) {  // TODO(jamesr): SOCK_DGRAM
        errno = EPROTOTYPE;
        return -1;
    }
    if (domain != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (protocol != 0) {
        errno = EPROTONOSUPPORT;
        return -1;
    }

    mxio_t* io[2];
    mx_status_t r = mxio_pipe_pair(&io[0], &io[1]);
    if (r != MX_OK) {
        errno = ERRNO(r);
        return -1;
    }

    if ((fd[0] = mxio_bind_to_fd(io[0], -1, 0)) < 0) {
        io[0]->ops->close(io[0]);
        mxio_release(io[0]);
        errno = ERRNO(EMFILE);
        return -1;
    }
    if ((fd[1] = mxio_bind_to_fd(io[1], -1, 0)) < 0) {
        io[1]->ops->close(io[1]);
        mxio_release(io[1]);
        close(fd[0]);
        errno = ERRNO(EMFILE);
        return -1;
    }
    return 0;
}

int sendmmsg(int fd, struct mmsghdr* msgvec, unsigned int vlen, unsigned int flags) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

int recvmmsg(int fd, struct mmsghdr* msgvec, unsigned int vlen, unsigned int flags, struct timespec* timeout) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

int sockatmark(int fd) {
    // ENOTTY is sic.
    return checksocket(fd, ENOTTY, ENOSYS);
}
