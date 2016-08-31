// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <magenta/processargs.h>

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <magenta/listnode.h>

// devhost rpc wrappers

static mx_status_t devhost_rpc(mx_handle_t h, devhost_msg_t* msg, mx_handle_t harg) {
    mx_status_t r;
    if ((r = mx_msgpipe_write(h, msg, sizeof(*msg), &harg, harg ? 1 : 0, 0)) < 0) {
        return r;
    }
    mx_signals_state_t pending;
    if ((r = mx_handle_wait_one(h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                      MX_TIME_INFINITE, &pending)) < 0) {
        return r;
    }
    if (pending.satisfied & MX_SIGNAL_PEER_CLOSED) {
        return ERR_CHANNEL_CLOSED;
    }
    uint32_t dsz = sizeof(*msg);
    if ((r = mx_msgpipe_read(h, msg, &dsz, NULL, NULL, 0)) < 0) {
        return r;
    }
    if ((dsz != sizeof(*msg)) || (msg->op != DH_OP_STATUS)) {
        return ERR_IO;
    }
    return msg->arg;
}

mx_status_t devhost_add(mx_device_t* dev, mx_device_t* parent) {
    iostate_t* ios;
    if ((ios = create_iostate(dev)) == NULL) {
        return ERR_NO_MEMORY;
    }
    mx_handle_t h[2];
    mx_status_t r;
    if ((r = mx_msgpipe_create(h, 0)) < 0) {
        free(ios);
        return r;
    }
    //printf("devhost_add(%p, %p)\n", dev, parent);
    devhost_msg_t msg;
    msg.op = DH_OP_ADD;
    msg.arg = 0;
    msg.device_id = parent->remote_id;
    msg.protocol_id = dev->protocol_id;
    memcpy(msg.namedata, dev->name, sizeof(dev->name));
    r = devhost_rpc(devhost_handle, &msg, h[1]);
    //printf("devhost_add() %d\n", r);
    if (r == NO_ERROR) {
        //printf("devhost: dev=%p remoted\n", dev);

        char tmp[MX_DEVICE_NAME_MAX + 9];
        snprintf(tmp, sizeof(tmp), "device:%s", dev->name);
        track_iostate(ios, tmp);

        dev->remote_id = msg.device_id;
        mxio_dispatcher_add(devmgr_rio_dispatcher, h[0], devmgr_rio_handler, ios);
    } else {
        printf("devhost: dev=%p name='%s' failed remoting %d\n", dev, dev->name, r);
        mx_handle_close(h[0]);
        free(ios);
    }
    return r;
}

mx_status_t devhost_remove(mx_device_t* dev) {
    devhost_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = DH_OP_REMOVE;
    msg.device_id = (uintptr_t)dev->remote_id;
    return devhost_rpc(devhost_handle, &msg, 0);
}

#if LIBDRIVER
mx_status_t devmgr_host_process(mx_device_t* dev, mx_driver_t* drv) {
    return ERR_NOT_SUPPORTED;
}
void track_iostate(iostate_t* ios, const char* fn) {
}
void untrack_iostate(iostate_t* ios) {
}
#endif
