// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "devmgr.h"

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

#include <system/listnode.h>

// devhost rpc wrappers

static mx_status_t devhost_rpc(mx_handle_t h, devhost_msg_t* msg, mx_handle_t harg) {
    mx_status_t r;
    if ((r = _magenta_message_write(h, msg, sizeof(*msg), &harg, harg ? 1 : 0, 0)) < 0) {
        return r;
    }
    mx_signals_t pending;
    if ((r = _magenta_handle_wait_one(h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                      MX_TIME_INFINITE, &pending, NULL)) < 0) {
        return r;
    }
    if (pending & MX_SIGNAL_PEER_CLOSED) {
        return ERR_CHANNEL_CLOSED;
    }
    uint32_t dsz = sizeof(*msg);
    if ((r = _magenta_message_read(h, msg, &dsz, NULL, NULL, 0)) < 0) {
        return r;
    }
    if ((dsz != sizeof(*msg)) || (msg->op != DH_OP_STATUS)) {
        return ERR_IO;
    }
    return msg->arg;
}

mx_status_t devhost_add(mx_device_t* dev, mx_device_t* parent) {
    diostate_t* ios;
    if ((ios = create_iostate(dev)) == NULL) {
        return ERR_NO_MEMORY;
    }
    mx_handle_t h0, h1;
    if ((h0 = _magenta_message_pipe_create(&h1)) < 0) {
        free(ios);
        return h0;
    }
    //printf("devhost_add(%p, %p)\n", dev, parent);
    devhost_msg_t msg;
    msg.op = DH_OP_ADD;
    msg.arg = 0;
    msg.device_id = parent->remote_id;
    msg.protocol_id = dev->protocol_id;
    memcpy(msg.namedata, dev->namedata, sizeof(dev->namedata));
    mx_status_t r = devhost_rpc(devhost_handle, &msg, h1);
    //printf("devhost_add() %d\n", r);
    if (r == NO_ERROR) {
        //printf("devhost: dev=%p remoted\n", dev);
        dev->remote_id = msg.device_id;
        mxio_dispatcher_add(devmgr_rio_dispatcher, h0, devmgr_rio_handler, ios);
    } else {
        printf("devhost: dev=%p name='%s' failed remoting %d\n", dev, dev->name, r);
        _magenta_handle_close(h0);
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
