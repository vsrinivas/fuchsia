// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Code shared between devhost and devmgr

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

#include "devcoordinator.h"

#if TRACE_PORT_API
#define zprintf(fmt...) printf(fmt)
#else
#define zprintf(fmt...) do {} while (0)
#endif

mx_status_t dc_msg_pack(dc_msg_t* msg, uint32_t* len_out,
                        const void* data, size_t datalen,
                        const char* name, const char* args) {
    uint32_t max = DC_MAX_DATA;
    uint8_t* ptr = msg->data;

    if (data) {
        if (datalen > max) {
            return ERR_BUFFER_TOO_SMALL;
        }
        memcpy(ptr, data, datalen);
        max -= datalen;
        ptr += datalen;
        msg->datalen = datalen;
    } else {
        msg->datalen = 0;
    }
    if (name) {
        datalen = strlen(name) + 1;
        if (datalen > max) {
            return ERR_BUFFER_TOO_SMALL;
        }
        memcpy(ptr, name, datalen);
        max -= datalen;
        ptr += datalen;
        msg->namelen = datalen;
    } else {
        msg->namelen = 0;
    }
    if (args) {
        datalen = strlen(args) + 1;
        if (datalen > max) {
            return ERR_BUFFER_TOO_SMALL;
        }
        memcpy(ptr, args, datalen);
        ptr += datalen;
        msg->argslen = datalen;
    } else {
        msg->argslen = 0;
    }
    *len_out = sizeof(dc_msg_t) - DC_MAX_DATA + (ptr - msg->data);
    return NO_ERROR;
}


mx_status_t dc_msg_unpack(dc_msg_t* msg, size_t len, const void** data,
                          const char** name, const char** args) {
    if (len < (sizeof(dc_msg_t) - DC_MAX_DATA)) {
        return ERR_BUFFER_TOO_SMALL;
    }
    len -= sizeof(dc_msg_t);
    uint8_t* ptr = msg->data;
    if (msg->datalen) {
        if (msg->datalen > len) {
            return ERR_BUFFER_TOO_SMALL;
        }
        *data = ptr;
        ptr += msg->datalen;
        len -= msg->datalen;
    } else {
        *data = NULL;
    }
    if (msg->namelen) {
        if (msg->namelen > len) {
            return ERR_BUFFER_TOO_SMALL;
        }
        *name = (char*) ptr;
        ptr[msg->namelen - 1] = 0;
        ptr += msg->namelen;
        len -= msg->namelen;
    } else {
        *name = "";
    }
    if (msg->argslen) {
        if (msg->argslen > len) {
            return ERR_BUFFER_TOO_SMALL;
        }
        *args = (char*) ptr;
        ptr[msg->argslen - 1] = 0;
    } else {
        *args = "";
    }
    return NO_ERROR;
}

mx_status_t dc_msg_rpc(mx_handle_t h, dc_msg_t* msg, size_t msglen,
                       mx_handle_t* handles, size_t hcount) {
    dc_status_t rsp;
    mx_channel_call_args_t args = {
        .wr_bytes = msg,
        .wr_handles = handles,
        .rd_bytes = &rsp,
        .rd_handles = NULL,
        .wr_num_bytes = msglen,
        .wr_num_handles = hcount,
        .rd_num_bytes = sizeof(rsp),
        .rd_num_handles = 0,
    };

    //TODO: incrementing txids
    msg->txid = 1;
    mx_status_t r;
    if ((r = mx_channel_call(h, 0, MX_TIME_INFINITE,
                             &args, &args.rd_num_bytes, &args.rd_num_handles,
                             NULL)) < 0) {
        for (size_t n = 0; n < hcount; n++) {
            mx_handle_close(handles[n]);
        }
        return r;
    }
    if (args.rd_num_bytes != sizeof(rsp)) {
        return ERR_INTERNAL;
    }

    return rsp.status;
}

mx_status_t port_init(port_t* port) {
    mx_status_t r = mx_port_create(MX_PORT_OPT_V2, &port->handle);
    zprintf("port_init(%p) port=%x\n", port, port->handle);
    return r;
}

mx_status_t port_watch(port_t* port, port_handler_t* ph) {
    zprintf("port_watch(%p, %p) obj=%x port=%x\n",
            port, ph, ph->handle, port->handle);
    return mx_object_wait_async(ph->handle, port->handle,
                                (uint64_t)(uintptr_t)ph,
                                ph->waitfor, MX_WAIT_ASYNC_ONCE);
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

mx_status_t port_dispatch(port_t* port, mx_time_t deadline) {
    for (;;) {
        mx_port_packet_t pkt;
        mx_status_t r;
        if ((r = mx_port_wait(port->handle, deadline, &pkt, 0)) != NO_ERROR) {
            if (r != ERR_TIMED_OUT) {
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
            if (ph->func(ph, pkt.signal.observed, 0) == NO_ERROR) {
                port_watch(port, ph);
            }
        }
        return NO_ERROR;
    }
}