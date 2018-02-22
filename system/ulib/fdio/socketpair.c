// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/socket.h>

#include <zircon/syscalls.h>

#include <errno.h>
#include <fdio/io.h>
#include <fdio/socket.h>
#include <fdio/util.h>

#include "pipe.h"
#include "private.h"
#include "unistd.h"

static int checksocket(int fd, int sock_err, int err) {
    fdio_t* io = fd_to_io(fd);
    if (io == NULL) {
        errno = EBADF;
        return -1;
    }
    int32_t is_socket = io->ioflag & IOFLAG_SOCKET;
    fdio_release(io);
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

static ssize_t zx_socketpair_recvfrom(fdio_t* io, void* data, size_t len, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    if (flags != 0 && flags != MSG_DONTWAIT) {
        return ZX_ERR_INVALID_ARGS;
    }
    zx_pipe_t* p = (zx_pipe_t*)io;
    int nonblock = (io->ioflag & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT);
    return zx_pipe_read_internal(p->h, data, len, nonblock);
}

static ssize_t zx_socketpair_sendto(fdio_t* io, const void* data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    if (flags != 0 && flags != MSG_DONTWAIT) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (addr != NULL) {
        return ZX_ERR_INVALID_ARGS;  // should set errno to EISCONN
    }
    zx_pipe_t* p = (zx_pipe_t*)io;
    int nonblock = (io->ioflag & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT);
    return zx_pipe_write_internal(p->h, data, len, nonblock);
}


static fdio_ops_t zx_socketpair_ops = {
    .read = zx_pipe_read,
    .read_at = fdio_default_read_at,
    .write = zx_pipe_write,
    .write_at = fdio_default_write_at,
    .recvfrom = zx_socketpair_recvfrom,
    .sendto = zx_socketpair_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .seek = fdio_default_seek,
    .misc = zx_pipe_misc,
    .close = zx_pipe_close,
    .open = fdio_default_open,
    .clone = zx_pipe_clone,
    .ioctl = fdio_default_ioctl,
    .wait_begin = zx_pipe_wait_begin,
    .wait_end = zx_pipe_wait_end,
    .unwrap = zx_pipe_unwrap,
    .shutdown = fdio_socketpair_shutdown,
    .posix_ioctl = zx_pipe_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
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

    fdio_t* io[2];
    zx_status_t r = fdio_pipe_pair(&io[0], &io[1]);
    if (r != ZX_OK) {
        errno = ERRNO(r);
        return -1;
    }
    io[0]->ops = &zx_socketpair_ops;
    io[1]->ops = &zx_socketpair_ops;

    if ((fd[0] = fdio_bind_to_fd(io[0], -1, 0)) < 0) {
        io[0]->ops->close(io[0]);
        fdio_release(io[0]);
        errno = ERRNO(EMFILE);
        return -1;
    }
    if ((fd[1] = fdio_bind_to_fd(io[1], -1, 0)) < 0) {
        io[1]->ops->close(io[1]);
        fdio_release(io[1]);
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

zx_status_t fdio_socketpair_shutdown(fdio_t* io, int how) {
    zx_pipe_t* p = (zx_pipe_t*)io;

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
    return zx_socket_write(p->h, options, NULL, 0, NULL);
}
