// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>

#include <mxio/util.h>
#include <mxio/remoteio.h>

#include "devcoordinator.h"

static mx_status_t dh_handle_open(mxrio_msg_t* msg, size_t len, mx_handle_t h, void* cookie) {
    printf("devhost: remoteio open\n");
    return ERR_NOT_SUPPORTED;
}

static mx_status_t dh_handle_rpc_read(mx_handle_t h, void* cookie) {
    dc_msg_t msg;
    mx_handle_t hin[2];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 2;

    mx_status_t r;
    if ((r = mx_channel_read(h, 0, &msg, msize, &msize,
                             hin, hcount, &hcount)) < 0) {
        return r;
    }

    // handle remoteio open messages only
    if ((msize >= MXRIO_HDR_SZ) && (msg.op == MXRIO_OPEN)) {
        if (hcount != 1) {
            r = ERR_INVALID_ARGS;
            goto fail;
        }
        return dh_handle_open((void*) &msg, msize, hin[0], cookie);
    }

    const void* data;
    const char* name;
    const char* args;
    if ((r = dc_msg_unpack(&msg, msize, &data, &name, &args)) < 0) {
        goto fail;
    }

    switch (msg.op) {
    case DC_OP_CREATE_DEVICE:
        printf("devhost: create device '%s'\n", name);
        return NO_ERROR;

    case DC_OP_BIND_DRIVER:
        printf("devhost: bind driver '%s'\n", name);
        return NO_ERROR;

    default:
        printf("devhost: invalid rpc op %08x\n", msg.op);
        r = ERR_NOT_SUPPORTED;
    }

fail:
    while (hcount > 0) {
        mx_handle_close(hin[--hcount]);
    }
    return r;
}

static mx_status_t dh_handle_rpc(port_handler_t* ph, mx_signals_t signals) {
    if (signals & MX_CHANNEL_READABLE) {
        mx_status_t r = dh_handle_rpc_read(ph->handle, NULL);
        if (r != NO_ERROR) {
            printf("devhost: devmgr rpc unhandleable %p\n", ph);
            exit(0);
        }
        return r;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        printf("devhost: devmgr disconnected!\n");
        exit(0);
    }
    printf("devhost: no work? %08x\n", signals);
    return NO_ERROR;
}

static port_t devhost_port;

static port_handler_t devhost_handler = {
    .waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
    .func = dh_handle_rpc,
};


static void devhost_io_init(void) {
    mx_handle_t h;
    if (mx_log_create(MX_LOG_FLAG_DEVICE, &h) < 0) {
        return;
    }
    mxio_t* logger;
    if ((logger = mxio_logger_create(h)) == NULL) {
        return;
    }
    close(1);
    mxio_bind_to_fd(logger, 1, 0);
    dup2(1, 2);
}

int main(int argc, char** argv) {
    devhost_io_init();

    printf("devhost: main()\n");

    devhost_handler.handle = mx_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_USER0, 0));
    if (devhost_handler.handle == MX_HANDLE_INVALID) {
        printf("devhost: rpc handle invalid\n");
        return -1;
    }

    mx_status_t r;
    if ((r = port_init(&devhost_port)) < 0) {
        printf("devhost: could not create port: %d\n", r);
        return -1;
    }
    if ((r = port_watch(&devhost_port, &devhost_handler)) < 0) {
        printf("devhost: could not watch rpc channel: %d\n", r);
        return -1;
    }
    r = port_dispatch(&devhost_port);
    printf("devhost: port dispatch finished: %d\n", r);

    return 0;
}