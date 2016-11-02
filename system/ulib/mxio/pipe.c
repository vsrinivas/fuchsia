// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include "private.h"

typedef struct mx_pipe {
    mxio_t io;
    mx_handle_t h;
} mx_pipe_t;

static ssize_t _read(mx_handle_t h, void* data, size_t len, int nonblock) {
    // TODO: let the generic read() to do this loop
    for (;;) {
        ssize_t r;
        if ((r = mx_socket_read(h, 0, data, len, &len)) == NO_ERROR) {
            return (ssize_t) len;
        }
        if (r == ERR_SHOULD_WAIT && !nonblock) {
            mx_signals_t pending;
            r = mx_handle_wait_one(h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SIGNAL_READABLE) {
                continue;
            }
            if (pending & MX_SIGNAL_PEER_CLOSED) {
                return ERR_REMOTE_CLOSED;
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
            r = mx_handle_wait_one(h, MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SIGNAL_WRITABLE) {
                continue;
            }
            if (pending & MX_SIGNAL_PEER_CLOSED) {
                return ERR_REMOTE_CLOSED;
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
        signals |= MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    }
    if (events & EPOLLOUT) {
        signals |= MX_SIGNAL_WRITABLE;
    }
    if (events & EPOLLRDHUP) {
        signals |= MX_SIGNAL_PEER_CLOSED;
    }
    *_signals = signals;
}

static void mx_pipe_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    uint32_t events = 0;
    if (signals & (MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED)) {
        events |= EPOLLIN;
    }
    if (signals & MX_SIGNAL_WRITABLE) {
        events |= EPOLLOUT;
    }
    if (signals & MX_SIGNAL_PEER_CLOSED) {
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
    types[0] = MX_HND_TYPE_MXIO_PIPE;
    return 1;
}

static mxio_ops_t mx_pipe_ops = {
    .read = mx_pipe_read,
    .write = mx_pipe_write,
    .seek = mxio_default_seek,
    .misc = mxio_default_misc,
    .close = mx_pipe_close,
    .open = mxio_default_open,
    .clone = mx_pipe_clone,
    .ioctl = mxio_default_ioctl,
    .wait_begin = mx_pipe_wait_begin,
    .wait_end = mx_pipe_wait_end,
};

mxio_t* mxio_pipe_create(mx_handle_t h) {
    mx_pipe_t* p = calloc(1, sizeof(*p));
    if (p == NULL)
        return NULL;
    p->io.ops = &mx_pipe_ops;
    p->io.magic = MXIO_MAGIC;
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
        mx_handle_close(h0);
        mx_handle_close(h1);
        return ERR_NO_MEMORY;
    }
    if ((b = mxio_pipe_create(h1)) == NULL) {
        mx_pipe_close(a);
        mx_handle_close(h1);
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
    types[0] = MX_HND_TYPE_MXIO_PIPE;
    types[1] = MX_HND_TYPE_MXIO_PIPE;
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
    *type = MX_HND_TYPE_MXIO_PIPE;
    return fd;

fail:
    mx_handle_close(h0);
    mx_handle_close(h1);
    return r;
}
