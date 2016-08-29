// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include "private.h"

typedef struct mx_pipe {
    mxio_t io;
    mx_handle_t h;
} mx_pipe_t;

static ssize_t _blocking_read(mx_handle_t h, void* data, size_t len) {
    for (;;) {
        ssize_t r;
        if ((r = mx_socket_read(h, 0, len, data)) >= 0) {
            return r;
        }
        if (r == ERR_SHOULD_WAIT) {
            mx_signals_state_t pending;
            r = mx_handle_wait_one(h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending.satisfied & MX_SIGNAL_READABLE) {
                continue;
            }
            if (pending.satisfied & MX_SIGNAL_PEER_CLOSED) {
                return ERR_REMOTE_CLOSED;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t _blocking_write(mx_handle_t h, const void* data, size_t len) {
    for (;;) {
        ssize_t r;
        if ((r = mx_socket_write(h, 0, len, data)) >= 0) {
            return r;
        }
        if (r == ERR_SHOULD_WAIT) {
            mx_signals_state_t pending;
            r = mx_handle_wait_one(h, MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending.satisfied & MX_SIGNAL_WRITABLE) {
                continue;
            }
            if (pending.satisfied & MX_SIGNAL_PEER_CLOSED) {
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
    return _blocking_write(p->h, data, len);
}

static ssize_t mx_pipe_read(mxio_t* io, void* data, size_t len) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    return _blocking_read(p->h, data, len);
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

static mx_status_t mx_pipe_wait(mxio_t* io, uint32_t _events, uint32_t* _pending, mx_time_t timeout) {
    mx_pipe_t* p = (void*)io;
    uint32_t events = 0;
    mx_status_t r;
    mx_signals_state_t pending;

    if (_events & MXIO_EVT_READABLE) {
        events |= MX_SIGNAL_READABLE;
    }
    if (_events & MXIO_EVT_WRITABLE) {
        events |= MX_SIGNAL_WRITABLE;
    }
    if ((r = mx_handle_wait_one(p->h, events, timeout, &pending)) < 0) {
        return r;
    }
    if (_pending) {
        uint32_t out = 0;
        if (pending.satisfied & MX_SIGNAL_READABLE) {
            out |= MXIO_EVT_READABLE;
        }
        if (pending.satisfied & MX_SIGNAL_WRITABLE) {
            out |= MXIO_EVT_WRITABLE;
        }
        *_pending = out;
    }
    return NO_ERROR;
}

static mx_status_t mx_pipe_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mx_pipe_t* p = (void*)io;
    handles[0] = mx_handle_duplicate(p->h, MX_RIGHT_SAME_RIGHTS);
    if (handles[0] < 0) {
        return handles[0];
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
    .wait = mx_pipe_wait,
    .ioctl = mxio_default_ioctl,
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
    mx_handle_t h[2];
    mxio_t *a, *b;
    mx_status_t r;
    if ((r = mx_socket_create(h, 0)) < 0) {
        return r;
    }
    if ((a = mxio_pipe_create(h[0])) == NULL) {
        mx_handle_close(h[0]);
        mx_handle_close(h[1]);
        return ERR_NO_MEMORY;
    }
    if ((b = mxio_pipe_create(h[1])) == NULL) {
        mx_pipe_close(a);
        mx_handle_close(h[1]);
        return ERR_NO_MEMORY;
    }
    *_a = a;
    *_b = b;
    return 0;
}

mx_status_t mxio_pipe_pair_raw(mx_handle_t* handles, uint32_t* types) {
    mx_status_t r;
    if ((r = mx_socket_create(handles, 0)) < 0) {
        return r;
    }
    types[0] = MX_HND_TYPE_MXIO_PIPE;
    types[1] = MX_HND_TYPE_MXIO_PIPE;
    return 2;
}

mx_status_t mxio_pipe_half(mx_handle_t* handle, uint32_t* type) {
    mx_handle_t h[2];
    mx_status_t r;
    mxio_t* io;
    int fd;
    if ((r = mx_socket_create(h, 0)) < 0) {
        return r;
    }
    if ((io = mxio_pipe_create(h[0])) == NULL) {
        r = ERR_NO_MEMORY;
        goto fail;
    }
    if ((fd = mxio_bind_to_fd(io, -1, 0)) < 0) {
        mxio_release(io);
        r = ERR_NO_RESOURCES;
        goto fail;
    }
    *handle = h[1];
    *type = MX_HND_TYPE_MXIO_PIPE;
    return fd;

fail:
    mx_handle_close(h[0]);
    mx_handle_close(h[1]);
    return r;
}
