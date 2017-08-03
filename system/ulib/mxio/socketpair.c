// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/socket.h>

#include <magenta/syscalls.h>

#include <errno.h>
#include <mxio/io.h>
#include <mxio/socket.h>
#include <mxio/util.h>

#include "pipe.h"
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

static ssize_t mx_socketpair_recvfrom(mxio_t* io, void* data, size_t len, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    if (flags != 0 && flags != MSG_DONTWAIT) {
        return MX_ERR_INVALID_ARGS;
    }
    mx_pipe_t* p = (mx_pipe_t*)io;
    int nonblock = (io->flags & MXIO_FLAG_NONBLOCK) || flags & MSG_DONTWAIT;
    return mx_pipe_read_internal(p->h, data, len, nonblock);
}

static ssize_t mx_socketpair_sendto(mxio_t* io, const void* data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    if (flags != 0 && flags != MSG_DONTWAIT) {
        return MX_ERR_INVALID_ARGS;
    }
    if (addr != NULL) {
        return MX_ERR_INVALID_ARGS;  // should set errno to EISCONN
    }
    mx_pipe_t* p = (mx_pipe_t*)io;
    int nonblock = (io->flags & MXIO_FLAG_NONBLOCK) || flags & MSG_DONTWAIT;
    return mx_pipe_write_internal(p->h, data, len, nonblock);
}


static mxio_ops_t mx_socketpair_ops = {
    .read = mx_pipe_read,
    .read_at = mxio_default_read_at,
    .write = mx_pipe_write,
    .write_at = mxio_default_write_at,
    .recvfrom = mx_socketpair_recvfrom,
    .sendto = mx_socketpair_sendto,
    .recvmsg = mxio_default_recvmsg,
    .sendmsg = mxio_default_sendmsg,
    .seek = mxio_default_seek,
    .misc = mx_pipe_misc,
    .close = mx_pipe_close,
    .open = mxio_default_open,
    .clone = mx_pipe_clone,
    .ioctl = mxio_default_ioctl,
    .wait_begin = mx_pipe_wait_begin,
    .wait_end = mx_pipe_wait_end,
    .unwrap = mx_pipe_unwrap,
    .shutdown = mxio_socketpair_shutdown,
    .posix_ioctl = mx_pipe_posix_ioctl,
    .get_vmo = mxio_default_get_vmo,
};


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
    io[0]->ops = &mx_socketpair_ops;
    io[1]->ops = &mx_socketpair_ops;

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

mx_status_t mxio_socketpair_shutdown(mxio_t* io, int how) {
    mx_pipe_t* p = (mx_pipe_t*)io;

    uint32_t options = 0;
    switch (how) {
    case SHUT_RD:
        options = MX_SOCKET_SHUTDOWN_READ;
        break;
    case SHUT_WR:
        options = MX_SOCKET_SHUTDOWN_WRITE;
        break;
    case SHUT_RDWR:
        options = MX_SOCKET_SHUTDOWN_READ | MX_SOCKET_SHUTDOWN_WRITE;
        break;
    }
    return mx_socket_write(p->h, options, NULL, 0, NULL);
}
