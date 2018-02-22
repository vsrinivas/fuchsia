// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <poll.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fdio/util.h>
#include <fdio/vfs.h>

#include "pipe.h"
#include "private.h"

ssize_t zx_pipe_read_internal(zx_handle_t h, void* data, size_t len, int nonblock) {
    // TODO: let the generic read() to do this loop
    for (;;) {
        size_t bytes_read;
        ssize_t r = zx_socket_read(h, 0, data, len, &bytes_read);
        if (r == ZX_OK) {
            // zx_socket_read() sets *actual to the number of bytes in the buffer when data is NULL
            // and len is 0. read() should return 0 in that case.
            if (len == 0) {
                return 0;
            } else {
                return (ssize_t)bytes_read;
            }
        } else if (r == ZX_ERR_PEER_CLOSED || r == ZX_ERR_BAD_STATE) {
            return 0;
        }
        if (r == ZX_ERR_SHOULD_WAIT && !nonblock) {
            zx_signals_t pending;
            r = zx_object_wait_one(h,
                                   ZX_SOCKET_READABLE | ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED,
                                   ZX_TIME_INFINITE,
                                   &pending);
            if (r < 0) {
                return r;
            }
            if (pending & ZX_SOCKET_READABLE) {
                continue;
            }
            if (pending & (ZX_SOCKET_READ_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
                return 0;
            }
            // impossible
            return ZX_ERR_INTERNAL;
        }
        return r;
    }
}

ssize_t zx_pipe_write_internal(zx_handle_t h, const void* data, size_t len, int nonblock) {
    // TODO: let the generic write() to do this loop
    for (;;) {
        ssize_t r;
        if ((r = zx_socket_write(h, 0, data, len, &len)) == ZX_OK) {
            return (ssize_t)len;
        }
        if (r == ZX_ERR_SHOULD_WAIT && !nonblock) {
            zx_signals_t pending;
            r = zx_object_wait_one(h,
                                   ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED,
                                   ZX_TIME_INFINITE,
                                   &pending);
            if (r < 0) {
                return r;
            }
            if (pending & ZX_SOCKET_WRITABLE) {
                continue;
            }
            if (pending & (ZX_SOCKET_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED)) {
                return ZX_ERR_PEER_CLOSED;
            }
            // impossible
            return ZX_ERR_INTERNAL;
        }
        return r;
    }
}


ssize_t zx_pipe_write(fdio_t* io, const void* data, size_t len) {
    zx_pipe_t* p = (zx_pipe_t*)io;
    return zx_pipe_write_internal(p->h, data, len, io->ioflag & IOFLAG_NONBLOCK);
}

ssize_t zx_pipe_read(fdio_t* io, void* data, size_t len) {
    zx_pipe_t* p = (zx_pipe_t*)io;
    return zx_pipe_read_internal(p->h, data, len, io->ioflag & IOFLAG_NONBLOCK);
}
zx_status_t zx_pipe_misc(fdio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* data, size_t len) {
    switch (op) {
    default:
        return ZX_ERR_NOT_SUPPORTED;

    case ZXRIO_STAT: {
        vnattr_t attr = {};
        if (maxreply < sizeof(attr)) {
            return ZX_ERR_INVALID_ARGS;
        }
        attr.mode = V_TYPE_PIPE | V_IRUSR | V_IWUSR;
        vnattr_t* attr_out = data;
        *attr_out = attr;
        return sizeof(attr);
    }
    case ZXRIO_FCNTL: {
        uint32_t* flags = (uint32_t*) data;
        if (flags) {
            *flags = 0;
        }
        return 0;
    }
    }
}

zx_status_t zx_pipe_close(fdio_t* io) {
    zx_pipe_t* p = (zx_pipe_t*)io;
    zx_handle_t h = p->h;
    p->h = 0;
    zx_handle_close(h);
    return 0;
}

void zx_pipe_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals) {
    zx_pipe_t* p = (void*)io;
    *handle = p->h;
    zx_signals_t signals = 0;
    if (events & POLLIN) {
        signals |= ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED;
    }
    if (events & POLLOUT) {
        signals |= ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED;
    }
    if (events & POLLRDHUP) {
        signals |= ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED;
    }
    *_signals = signals;
}

void zx_pipe_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
    uint32_t events = 0;
    if (signals & (ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED)) {
        events |= POLLIN;
    }
    if (signals & (ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED)) {
        events |= POLLOUT;
    }
    if (signals & (ZX_SOCKET_PEER_CLOSED | ZX_SOCKET_READ_DISABLED)) {
        events |= POLLRDHUP;
    }
    *_events = events;
}

zx_status_t zx_pipe_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zx_pipe_t* p = (void*)io;
    zx_status_t status = zx_handle_duplicate(p->h, ZX_RIGHT_SAME_RIGHTS, &handles[0]);
    if (status < 0) {
        return status;
    }
    types[0] = PA_FDIO_PIPE;
    return 1;
}

zx_status_t zx_pipe_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zx_pipe_t* p = (void*)io;
    handles[0] = p->h;
    types[0] = PA_FDIO_PIPE;
    return 1;
}

ssize_t zx_pipe_posix_ioctl(fdio_t* io, int req, va_list va) {
    zx_pipe_t* p = (void*)io;
    switch (req) {
    case FIONREAD: {
        zx_status_t r;
        size_t avail;
        if ((r = zx_socket_read(p->h, 0, NULL, 0, &avail)) < 0) {
            return r;
        }
        if (avail > INT_MAX) {
            avail = INT_MAX;
        }
        int* actual = va_arg(va, int*);
        *actual = avail;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static fdio_ops_t zx_pipe_ops = {
    .read = zx_pipe_read,
    .read_at = fdio_default_read_at,
    .write = zx_pipe_write,
    .write_at = fdio_default_write_at,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
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
    .shutdown = fdio_default_shutdown,
    .posix_ioctl = zx_pipe_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
};

fdio_t* fdio_pipe_create(zx_handle_t h) {
    zx_pipe_t* p = fdio_alloc(sizeof(*p));
    if (p == NULL) {
        zx_handle_close(h);
        return NULL;
    }
    p->io.ops = &zx_pipe_ops;
    p->io.magic = FDIO_MAGIC;
    atomic_init(&p->io.refcount, 1);
    p->h = h;
    return &p->io;
}

int fdio_pipe_pair(fdio_t** _a, fdio_t** _b) {
    zx_handle_t h0, h1;
    fdio_t *a, *b;
    zx_status_t r;
    if ((r = zx_socket_create(0, &h0, &h1)) < 0) {
        return r;
    }
    if ((a = fdio_pipe_create(h0)) == NULL) {
        zx_handle_close(h1);
        return ZX_ERR_NO_MEMORY;
    }
    if ((b = fdio_pipe_create(h1)) == NULL) {
        zx_pipe_close(a);
        return ZX_ERR_NO_MEMORY;
    }
    *_a = a;
    *_b = b;
    return 0;
}

zx_status_t fdio_pipe_pair_raw(zx_handle_t* handles, uint32_t* types) {
    zx_status_t r;
    if ((r = zx_socket_create(0, handles, handles + 1)) < 0) {
        return r;
    }
    types[0] = PA_FDIO_PIPE;
    types[1] = PA_FDIO_PIPE;
    return 2;
}

zx_status_t fdio_pipe_half(zx_handle_t* handle, uint32_t* type) {
    zx_handle_t h0, h1;
    zx_status_t r;
    fdio_t* io;
    int fd;
    if ((r = zx_socket_create(0, &h0, &h1)) < 0) {
        return r;
    }
    if ((io = fdio_pipe_create(h0)) == NULL) {
        r = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    if ((fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
        fdio_release(io);
        r = ZX_ERR_NO_RESOURCES;
        goto fail;
    }
    *handle = h1;
    *type = PA_FDIO_PIPE;
    return fd;

fail:
    zx_handle_close(h1);
    return r;
}
