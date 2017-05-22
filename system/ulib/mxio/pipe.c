// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>
#include <mxio/vfs.h>

#include "private.h"

typedef struct mx_pipe {
    mxio_t io;
    mx_handle_t h;
} mx_pipe_t;

static ssize_t _read(mx_handle_t h, void* data, size_t len, int nonblock) {
    // TODO: let the generic read() to do this loop
    for (;;) {
        ssize_t r = mx_socket_read(h, 0, data, len, &len);
        if (r == NO_ERROR) {
            return (ssize_t) len;
        } else if (r == ERR_PEER_CLOSED) {
            return 0;
        }
        if (r == ERR_SHOULD_WAIT && !nonblock) {
            mx_signals_t pending;
            r = mx_object_wait_one(h, MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SOCKET_READABLE) {
                continue;
            }
            if (pending & MX_SOCKET_PEER_CLOSED) {
                return 0;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t _write(mx_handle_t h, const void* data, size_t len, int nonblock) {
    // TODO: let the generic write() to do this loop
    for (;;) {
        ssize_t r;
        if ((r = mx_socket_write(h, 0, data, len, &len)) == NO_ERROR) {
            return (ssize_t)len;
        }
        if (r == ERR_SHOULD_WAIT && !nonblock) {
            mx_signals_t pending;
            r = mx_object_wait_one(h, MX_SOCKET_WRITABLE | MX_SOCKET_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SOCKET_WRITABLE) {
                continue;
            }
            if (pending & MX_SOCKET_PEER_CLOSED) {
                return ERR_PEER_CLOSED;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return r;
    }
}


static ssize_t mx_pipe_write(mxio_t* io, const void* data, size_t len) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    return _write(p->h, data, len, io->flags & MXIO_FLAG_NONBLOCK);
}

static ssize_t mx_pipe_read(mxio_t* io, void* data, size_t len) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    return _read(p->h, data, len, io->flags & MXIO_FLAG_NONBLOCK);
}

static mx_status_t mx_pipe_misc(mxio_t* io, uint32_t op, int64_t off, uint32_t maxreply, void* data, size_t len) {
    switch (op) {
    default:
        return ERR_NOT_SUPPORTED;

    case MXRIO_STAT: {
        vnattr_t attr = {};
        if (maxreply < sizeof(attr)) {
            return ERR_INVALID_ARGS;
        }
        attr.mode = V_TYPE_PIPE | V_IRUSR | V_IWUSR;
        vnattr_t* attr_out = data;
        *attr_out = attr;
        return sizeof(attr);
    }
    }
}

static mx_status_t mx_pipe_close(mxio_t* io) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    mx_handle_t h = p->h;
    p->h = 0;
    mx_handle_close(h);
    return 0;
}

static void mx_pipe_release(mxio_t* io) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    mx_handle_close(p->h);
    free(io);
}

static void mx_pipe_wait_begin(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* _signals) {
    mx_pipe_t* p = (void*)io;
    *handle = p->h;
    mx_signals_t signals = 0;
    if (events & EPOLLIN) {
        signals |= MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED;
    }
    if (events & EPOLLOUT) {
        signals |= MX_SOCKET_WRITABLE;
    }
    if (events & EPOLLRDHUP) {
        signals |= MX_SOCKET_PEER_CLOSED;
    }
    *_signals = signals;
}

static void mx_pipe_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    uint32_t events = 0;
    if (signals & (MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED)) {
        events |= EPOLLIN;
    }
    if (signals & MX_SOCKET_WRITABLE) {
        events |= EPOLLOUT;
    }
    if (signals & MX_SOCKET_PEER_CLOSED) {
        events |= EPOLLRDHUP;
    }
    *_events = events;
}

static mx_status_t mx_pipe_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mx_pipe_t* p = (void*)io;
    mx_status_t status = mx_handle_duplicate(p->h, MX_RIGHT_SAME_RIGHTS, &handles[0]);
    if (status < 0) {
        return status;
    }
    types[0] = PA_MXIO_PIPE;
    return 1;
}

static mx_status_t mx_pipe_unwrap(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mx_pipe_t* p = (void*)io;
    handles[0] = p->h;
    types[0] = PA_MXIO_PIPE;
    free(p);
    return 1;
}

static ssize_t mx_pipe_posix_ioctl(mxio_t* io, int req, va_list va) {
    mx_pipe_t* p = (void*)io;
    switch (req) {
    case FIONREAD: {
        mx_status_t r;
        size_t avail;
        if ((r = mx_socket_read(p->h, 0, NULL, 0, &avail)) < 0) {
            return r;
        }
        if (avail > INT_MAX) {
            avail = INT_MAX;
        }
        int* actual = va_arg(va, int*);
        *actual = avail;
        return NO_ERROR;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mxio_ops_t mx_pipe_ops = {
    .read = mx_pipe_read,
    .write = mx_pipe_write,
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
    .posix_ioctl = mx_pipe_posix_ioctl,
    .get_vmo = mxio_default_get_vmo,
};

mxio_t* mxio_pipe_create(mx_handle_t h) {
    mx_pipe_t* p = calloc(1, sizeof(*p));
    if (p == NULL) {
        mx_handle_close(h);
        return NULL;
    }
    p->io.ops = &mx_pipe_ops;
    p->io.magic = MXIO_MAGIC;
    p->io.flags |= MXIO_FLAG_PIPE;
    atomic_init(&p->io.refcount, 1);
    p->h = h;
    return &p->io;
}

int mxio_pipe_pair(mxio_t** _a, mxio_t** _b) {
    mx_handle_t h0, h1;
    mxio_t *a, *b;
    mx_status_t r;
    if ((r = mx_socket_create(0, &h0, &h1)) < 0) {
        return r;
    }
    if ((a = mxio_pipe_create(h0)) == NULL) {
        mx_handle_close(h1);
        return ERR_NO_MEMORY;
    }
    if ((b = mxio_pipe_create(h1)) == NULL) {
        mx_pipe_close(a);
        return ERR_NO_MEMORY;
    }
    *_a = a;
    *_b = b;
    return 0;
}

mx_status_t mxio_pipe_pair_raw(mx_handle_t* handles, uint32_t* types) {
    mx_status_t r;
    if ((r = mx_socket_create(0, handles, handles + 1)) < 0) {
        return r;
    }
    types[0] = PA_MXIO_PIPE;
    types[1] = PA_MXIO_PIPE;
    return 2;
}

mx_status_t mxio_pipe_half(mx_handle_t* handle, uint32_t* type) {
    mx_handle_t h0, h1;
    mx_status_t r;
    mxio_t* io;
    int fd;
    if ((r = mx_socket_create(0, &h0, &h1)) < 0) {
        return r;
    }
    if ((io = mxio_pipe_create(h0)) == NULL) {
        r = ERR_NO_MEMORY;
        goto fail;
    }
    if ((fd = mxio_bind_to_fd(io, -1, 0)) < 0) {
        mxio_release(io);
        r = ERR_NO_RESOURCES;
        goto fail;
    }
    *handle = h1;
    *type = PA_MXIO_PIPE;
    return fd;

fail:
    mx_handle_close(h1);
    return r;
}
