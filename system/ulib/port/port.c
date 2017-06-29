// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

#include <mxio/private.h>

#include <port/port.h>

#if TRACE_PORT_API
#define zprintf(fmt...) printf(fmt)
#else
#define zprintf(fmt...) do {} while (0)
#endif

mx_status_t port_init(port_t* port) {
    mx_status_t r = mx_port_create(0, &port->handle);
    zprintf("port_init(%p) port=%x\n", port, port->handle);
    return r;
}

mx_status_t port_wait(port_t* port, port_handler_t* ph) {
    zprintf("port_wait(%p, %p) obj=%x port=%x\n",
            port, ph, ph->handle, port->handle);
    return mx_object_wait_async(ph->handle, port->handle,
                                (uint64_t)(uintptr_t)ph,
                                ph->waitfor, MX_WAIT_ASYNC_ONCE);
}

mx_status_t port_wait_repeating(port_t* port, port_handler_t* ph) {
    zprintf("port_wait_repeating(%p, %p) obj=%x port=%x\n",
            port, ph, ph->handle, port->handle);
    return mx_object_wait_async(ph->handle, port->handle,
                                (uint64_t)(uintptr_t)ph,
                                ph->waitfor, MX_WAIT_ASYNC_REPEATING);
}


mx_status_t port_cancel(port_t* port, port_handler_t* ph) {
    mx_status_t r = mx_port_cancel(port->handle, ph->handle,
                                   (uint64_t)(uintptr_t)ph);
    zprintf("port_cancel(%p, %p) obj=%x port=%x: r = %d\n",
            port, ph, ph->handle, port->handle, r);
    return r;
}

mx_status_t port_queue(port_t* port, port_handler_t* ph, uint32_t evt) {
    mx_port_packet_t pkt;
    pkt.key = (uintptr_t)ph;
    pkt.user.u32[0] = evt;
    mx_status_t r = mx_port_queue(port->handle, &pkt, 0);
    zprintf("port_queue(%p, %p) obj=%x port=%x evt=%x: r=%d\n",
            port, ph, ph->handle, port->handle, r, evt);
    return r;
}

mx_status_t port_dispatch(port_t* port, mx_time_t deadline, bool once) {
    for (;;) {
        mx_port_packet_t pkt;
        mx_status_t r;
        if ((r = mx_port_wait(port->handle, deadline, &pkt, 0)) != MX_OK) {
            if (r != MX_ERR_TIMED_OUT) {
                printf("port_dispatch: port wait failed %d\n", r);
            }
            return r;
        }
        port_handler_t* ph = (void*) (uintptr_t) pkt.key;
        if (pkt.type == MX_PKT_TYPE_USER) {
            zprintf("port_dispatch(%p) port=%x ph=%p func=%p: evt=%x\n",
                    port, port->handle, ph, ph->func, pkt.user.u32[0]);
            ph->func(ph, 0, pkt.user.u32[0]);
        } else {
            zprintf("port_dispatch(%p) port=%x ph=%p func=%p: signals=%x\n",
                    port, port->handle, ph, ph->func, pkt.signal.observed);
            if (ph->func(ph, pkt.signal.observed, 0) == MX_OK) {
                port_wait(port, ph);
            }
        }
        if (once) {
            return MX_OK;
        }
    }
}

static mx_status_t port_fd_handler_func(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    port_fd_handler_t* fh = (void*) ph;

    if (evt) {
        return fh->func(fh, 0, evt);
    } else {
        uint32_t pollevt;
        __mxio_wait_end(fh->mxio_context, signals, &pollevt);
        return fh->func(fh, pollevt, 0);
    }
}

mx_status_t port_fd_handler_init(port_fd_handler_t* fh, int fd, unsigned pollevt) {
    mxio_t* io = __mxio_fd_to_io(fd);
    if (io == NULL) {
        return MX_ERR_INVALID_ARGS;
    }
    __mxio_wait_begin(io, pollevt, &fh->ph.handle, &fh->ph.waitfor);
    fh->ph.func = port_fd_handler_func;
    fh->mxio_context = io;
    return MX_OK;
}

void port_fd_handler_done(port_fd_handler_t* fh) {
    __mxio_release(fh->mxio_context);
    fh->mxio_context = NULL;
    fh->ph.handle = MX_HANDLE_INVALID;
    fh->ph.waitfor = 0;
}
