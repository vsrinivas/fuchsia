// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/socket.h>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <errno.h>
#include <lib/fdio/io.h>
#include <lib/fdio/socket.h>
#include <lib/fdio/util.h>

#include "pipe.h"
#include "private.h"
#include "unistd.h"

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

static zx_status_t zx_socketpair_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zx_status_t status = zx_pipe_clone(io, handles, types);
    if (status < 0)
        return status;
    types[0] = PA_FDIO_SOCKETPAIR;
    return status;
}

static zx_status_t zx_socketpair_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zx_status_t status = zx_pipe_unwrap(io, handles, types);
    if (status < 0)
        return status;
    types[0] = PA_FDIO_SOCKETPAIR;
    return status;
}

static zx_status_t zx_socketpair_create(zx_handle_t h, int* fd) {
    fdio_t* io;
    if ((io = fdio_socketpair_create(h)) == NULL)
        return ZX_ERR_NO_MEMORY;
    if ((*fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
        io->ops->close(io);
        fdio_release(io);
        return ZX_ERR_NO_MEMORY;
    }
    return ZX_OK;
}

static fdio_ops_t zx_socketpair_ops = {
    .read = zx_pipe_read,
    .read_at = fdio_default_read_at,
    .write = zx_pipe_write,
    .write_at = fdio_default_write_at,
    .seek = fdio_default_seek,
    .misc = fdio_default_misc,
    .close = zx_pipe_close,
    .open = fdio_default_open,
    .clone = zx_socketpair_clone,
    .ioctl = fdio_default_ioctl,
    .wait_begin = zx_pipe_wait_begin,
    .wait_end = zx_pipe_wait_end,
    .unwrap = zx_socketpair_unwrap,
    .posix_ioctl = zx_pipe_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = zx_pipe_get_attr,
    .set_attr = fdio_default_set_attr,
    .sync = fdio_default_sync,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_default_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .recvfrom = zx_socketpair_recvfrom,
    .sendto = zx_socketpair_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .shutdown = fdio_socketpair_shutdown,
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

    zx_handle_t h[2];
    zx_status_t r = zx_socket_create(0, &h[0], &h[1]);
    if (r < 0)
        return r;
    if ((r = zx_socketpair_create(h[0], &fd[0])) < 0) {
        zx_handle_close(h[1]);
        return STATUS(r);
    }
    if ((r = zx_socketpair_create(h[1], &fd[1])) < 0) {
        close(fd[0]);
        return STATUS(r);
    }
    return 0;
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

fdio_t* fdio_socketpair_create(zx_handle_t h) {
    fdio_t* io;
    if ((io = fdio_pipe_create(h)) == NULL)
        return NULL;
    io->ops = &zx_socketpair_ops;
    return io;
}
