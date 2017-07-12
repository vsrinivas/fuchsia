// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>

#include <magenta/syscalls.h>
#include <mxio/io.h>

#include "private.h"

typedef struct mxwio mxwio_t;
struct mxwio {
    // base mxio io object
    mxio_t io;

    // arbitrary handle
    mx_handle_t h;

    // signals that cause POLLIN
    mx_signals_t signals_in;

    // signals that cause POLLOUT
    mx_signals_t signals_out;

    // if true, don't close handle on close() op
    bool shared_handle;
};

static mx_status_t mxwio_close(mxio_t* io) {
    mxwio_t* wio = (mxwio_t*)io;
    mx_handle_t h = wio->h;
    wio->h = MX_HANDLE_INVALID;
    if (!wio->shared_handle) {
        mx_handle_close(h);
    }
    return MX_OK;
}

static void mxwio_wait_begin(mxio_t* io, uint32_t events, mx_handle_t* handle,
                             mx_signals_t* _signals) {
    mxwio_t* wio = (void*)io;
    *handle = wio->h;
    mx_signals_t signals = 0;
    if (events & POLLIN) {
        signals |= wio->signals_in;
    }
    if (events & POLLOUT) {
        signals |= wio->signals_out;
    }
    *_signals = signals;
}

static void mxwio_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    mxwio_t* wio = (void*)io;
    uint32_t events = 0;
    if (signals & wio->signals_in) {
        events |= POLLIN;
    }
    if (signals & wio->signals_out) {
        events |= POLLOUT;
    }
    *_events = events;
}

static mxio_ops_t mxio_waitable_ops = {
    .read = mxio_default_read,
    .read_at = mxio_default_read_at,
    .write = mxio_default_write,
    .write_at = mxio_default_write_at,
    .recvfrom = mxio_default_recvfrom,
    .sendto = mxio_default_sendto,
    .recvmsg = mxio_default_recvmsg,
    .sendmsg = mxio_default_sendmsg,
    .seek = mxio_default_seek,
    .misc = mxio_default_misc,
    .close = mxwio_close,
    .open = mxio_default_open,
    .clone = mxio_default_clone,
    .ioctl = mxio_default_ioctl,
    .unwrap = mxio_default_unwrap,
    .shutdown = mxio_default_shutdown,
    .wait_begin = mxwio_wait_begin,
    .wait_end = mxwio_wait_end,
    .posix_ioctl = mxio_default_posix_ioctl,
};

mxio_t* mxio_waitable_create(mx_handle_t h, mx_signals_t signals_in,
                             mx_signals_t signals_out, bool shared_handle) {
    mxwio_t* wio = calloc(1, sizeof(*wio));
    if (wio == NULL) {
        if (!shared_handle) {
            mx_handle_close(h);
        }
        return NULL;
    }
    wio->io.ops = &mxio_waitable_ops;
    wio->io.magic = MXIO_MAGIC;
    wio->io.refcount = 1;
    wio->io.flags |= MXIO_FLAG_WAITABLE;
    wio->h = h;
    wio->signals_in = signals_in;
    wio->signals_out = signals_out;
    wio->shared_handle = shared_handle;
    return &wio->io;
}
