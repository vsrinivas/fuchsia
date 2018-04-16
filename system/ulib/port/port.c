// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <fdio/private.h>

#include <port/port.h>

#if TRACE_PORT_API
#define zprintf(fmt...) printf(fmt)
#else
#define zprintf(fmt...) do {} while (0)
#endif

zx_status_t port_init(port_t* port) {
    zx_status_t r = zx_port_create(0, &port->handle);
    zprintf("port_init(%p) port=%x\n", port, port->handle);
    return r;
}

zx_status_t port_wait(port_t* port, port_handler_t* ph) {
    zprintf("port_wait(%p, %p) obj=%x port=%x\n",
            port, ph, ph->handle, port->handle);
    return zx_object_wait_async(ph->handle, port->handle,
                                (uint64_t)(uintptr_t)ph,
                                ph->waitfor, ZX_WAIT_ASYNC_ONCE);
}

zx_status_t port_wait_repeating(port_t* port, port_handler_t* ph) {
    zprintf("port_wait_repeating(%p, %p) obj=%x port=%x\n",
            port, ph, ph->handle, port->handle);
    return zx_object_wait_async(ph->handle, port->handle,
                                (uint64_t)(uintptr_t)ph,
                                ph->waitfor, ZX_WAIT_ASYNC_REPEATING);
}


zx_status_t port_cancel(port_t* port, port_handler_t* ph) {
    zx_status_t r = zx_port_cancel(port->handle, ph->handle,
                                   (uint64_t)(uintptr_t)ph);
    zprintf("port_cancel(%p, %p) obj=%x port=%x: r = %d\n",
            port, ph, ph->handle, port->handle, r);
    return r;
}

zx_status_t port_queue(port_t* port, port_handler_t* ph, uint32_t evt) {
    zx_port_packet_t pkt;
    pkt.key = (uintptr_t)ph;
    pkt.user.u32[0] = evt;
    zx_status_t r = zx_port_queue(port->handle, &pkt, 1);
    zprintf("port_queue(%p, %p) obj=%x port=%x evt=%x: r=%d\n",
            port, ph, ph->handle, port->handle, r, evt);
    return r;
}

zx_status_t port_dispatch(port_t* port, zx_time_t deadline, bool once) {
    for (;;) {
        zx_port_packet_t pkt;
        zx_status_t r;
        if ((r = zx_port_wait(port->handle, deadline, &pkt, 1)) != ZX_OK) {
            if (r != ZX_ERR_TIMED_OUT) {
                printf("port_dispatch: port wait failed %d\n", r);
            }
            return r;
        }
        port_handler_t* ph = (void*) (uintptr_t) pkt.key;
        if (pkt.type == ZX_PKT_TYPE_USER) {
            zprintf("port_dispatch(%p) port=%x ph=%p func=%p: evt=%x\n",
                    port, port->handle, ph, ph->func, pkt.user.u32[0]);
            ph->func(ph, 0, pkt.user.u32[0]);
        } else {
            zprintf("port_dispatch(%p) port=%x ph=%p func=%p: signals=%x\n",
                    port, port->handle, ph, ph->func, pkt.signal.observed);
            if (ph->func(ph, pkt.signal.observed, 0) == ZX_OK) {
                port_wait(port, ph);
            }
        }
        if (once) {
            return ZX_OK;
        }
    }
}

static zx_status_t port_fd_handler_func(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    port_fd_handler_t* fh = (void*) ph;

    if (evt) {
        return fh->func(fh, 0, evt);
    } else {
        uint32_t pollevt;
        __fdio_wait_end(fh->fdio_context, signals, &pollevt);
        return fh->func(fh, pollevt, 0);
    }
}

zx_status_t port_fd_handler_init(port_fd_handler_t* fh, int fd, unsigned pollevt) {
    fdio_t* io = __fdio_fd_to_io(fd);
    if (io == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }
    __fdio_wait_begin(io, pollevt, &fh->ph.handle, &fh->ph.waitfor);
    fh->ph.func = port_fd_handler_func;
    fh->fdio_context = io;
    return ZX_OK;
}

void port_fd_handler_done(port_fd_handler_t* fh) {
    __fdio_release(fh->fdio_context);
    fh->fdio_context = NULL;
    fh->ph.handle = ZX_HANDLE_INVALID;
    fh->ph.waitfor = 0;
}
