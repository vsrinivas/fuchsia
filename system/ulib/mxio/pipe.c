// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
    uint32_t avail;
    uint32_t flags;
    uint8_t* next;
    uint8_t data[MXIO_CHUNK_SIZE];
} mx_pipe_t;

static mx_status_t mxu_blocking_read(mx_handle_t h, void* data, size_t len) {
    mx_signals_state_t pending;
    mx_status_t r;
    uint32_t sz;

    for (;;) {
        sz = len;
        r = mx_message_read(h, data, &sz, NULL, NULL, 0);
        if (r == 0) {
            return sz;
        }
        if (r == ERR_BAD_STATE) {
            r = mx_handle_wait_one(h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                         MX_TIME_INFINITE, &pending);
            if (r < 0)
                return r;
            if (pending.satisfied & MX_SIGNAL_READABLE)
                continue;
            if (pending.satisfied & MX_SIGNAL_PEER_CLOSED)
                return ERR_CHANNEL_CLOSED;
            return ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t mx_pipe_write(mxio_t* io, const void* _data, size_t len) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    const uint8_t* data = _data;
    mx_status_t r = 0;
    ssize_t count = 0;

    while (len > 0) {
        size_t xfer = (len > MXIO_CHUNK_SIZE) ? MXIO_CHUNK_SIZE : len;
        r = mx_message_write(p->h, data, xfer, NULL, 0, 0);
        if (r < 0)
            break;
        len -= xfer;
        count += xfer;
        data += xfer;
    }

    // prioritize partial write results over errors
    return count ? count : r;
}

static ssize_t mx_pipe_read(mxio_t* io, void* _data, size_t len) {
    mx_pipe_t* p = (mx_pipe_t*)io;
    uint8_t* data = _data;
    mx_status_t r = 0;
    ssize_t count = 0;

    while (len > 0) {
        if (p->avail == 0) {
            if (len >= MXIO_CHUNK_SIZE) {
                // largest message will fit, read directly
                r = mxu_blocking_read(p->h, data, MXIO_CHUNK_SIZE);
                if (r <= 0)
                    break;
                data += r;
                len -= r;
                count += r;
                continue;
            } else {
                r = mxu_blocking_read(p->h, p->data, MXIO_CHUNK_SIZE);
                if (r <= 0)
                    break;
                p->avail = r;
                p->next = p->data;
            }
        }
        size_t xfer = (p->avail > len) ? len : p->avail;
        memcpy(data, p->next, xfer);
        p->next += xfer;
        p->avail -= xfer;
        data += xfer;
        len -= xfer;
        count += xfer;
    }

    // prioritize partial read results over errors
    return count ? count : r;
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

static mxio_ops_t mx_pipe_ops = {
    .read = mx_pipe_read,
    .write = mx_pipe_write,
    .seek = mxio_default_seek,
    .misc = mxio_default_misc,
    .close = mx_pipe_close,
    .open = mxio_default_open,
    .clone = mxio_default_clone,
    .wait = mx_pipe_wait,
    .ioctl = mxio_default_ioctl,
};

mxio_t* mxio_pipe_create(mx_handle_t h) {
    mx_pipe_t* p = calloc(1, sizeof(*p));
    if (p == NULL)
        return NULL;
    p->io.ops = &mx_pipe_ops;
    p->io.magic = MXIO_MAGIC;
    p->io.refcount = 1;
    p->h = h;
    return &p->io;
}

int mxio_pipe_pair(mxio_t** _a, mxio_t** _b) {
    mx_handle_t h[2];
    mxio_t *a, *b;
    mx_status_t r = mx_message_pipe_create(h, 0);
    if (r < 0)
        return r;
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
    if ((r = mx_message_pipe_create(handles, 0)) < 0) {
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
    if ((r = mx_message_pipe_create(h, 0)) < 0) {
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