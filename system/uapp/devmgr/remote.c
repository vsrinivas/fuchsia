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

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/char.h>

#include <mxu/list.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>

#define SINGLE_PROCESS 0

static mx_driver_t proxy_driver = {
    .name = "proxy",
};

extern mxio_dispatcher_t* devmgr_dispatcher;

static list_node_t devhost_list = LIST_INITIAL_VALUE(devhost_list);

typedef struct devhost devhost_t;
typedef struct proxy proxy_t;

struct proxy {
    mx_device_t device;
    list_node_t node;
};

static mx_status_t proxy_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t proxy_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t proxy_release(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t proxy_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = proxy_open,
    .close = proxy_close,
    .release = proxy_release,
};

struct devhost {
    mx_handle_t handle;
    // message pipe the devhost uses to make requests of devmgr;

    list_node_t devices;
    // list of remoted devices associated with this devhost

    list_node_t node;
    // entry in devhost_list

    mx_device_t* root;
    // the local object that is the root (id 0) object to remote
};

static mx_device_t* devhost_id_to_dev(devhost_t* dh, uintptr_t id) {
    proxy_t* proxy;
    mx_device_t* dev = (mx_device_t*)id;
    list_for_every_entry (&dh->devices, proxy, proxy_t, node) {
        if (&proxy->device == dev) {
            return dev;
        }
    }
    return NULL;
}

static mx_status_t devhost_remote_add(devhost_t* dh, devhost_msg_t* msg, mx_handle_t h) {
    mx_status_t r = NO_ERROR;
    mx_device_t* dev;

    if (msg->device_id) {
        dev = devhost_id_to_dev(dh, msg->device_id);
    } else {
        dev = dh->root;
    }
    //printf("devmgr: remote %p add %p %x: dev=%p\n", dh, (void*)msg->device_id, h, dev);
    if (dev == NULL) {
        r = ERR_NOT_FOUND;
        goto fail0;
    }
    proxy_t* proxy;
    if ((proxy = malloc(sizeof(proxy_t))) == NULL) {
        r = ERR_NO_MEMORY;
        goto fail0;
    }
    if ((r = devmgr_device_init(&proxy->device, &proxy_driver,
                                msg->namedata, &proxy_device_proto)) < 0) {
        goto fail1;
    }
    proxy->device.remote = h;
    proxy->device.flags |= DEV_FLAG_REMOTE;
    proxy->device.protocol_id = msg->protocol_id;
    if ((r = devmgr_device_add(&proxy->device, dev)) < 0) {
        printf("devmgr: remote add failed %d\n", r);
        goto fail1;
    }
    list_add_tail(&dh->devices, &proxy->node);

    msg->device_id = (uintptr_t)&proxy->device;
    return NO_ERROR;
fail1:
    free(proxy);
fail0:
    _magenta_handle_close(h);
    return r;
}

static mx_status_t devhost_remote_remove(devhost_t* dh, devhost_msg_t* msg) {
    mx_device_t* dev = devhost_id_to_dev(dh, msg->device_id);
    //printf("devmgr: remote %p remove %p: dev=%p\n", dh, (void*)msg->device_id, dev);
    if (dev == NULL) {
        return ERR_NOT_FOUND;
    }
    return ERR_NOT_SUPPORTED;
}

static void devhost_remote_died(devhost_t* dh) {
    printf("devmgr: remote %p died\n", dh);
}

// handle devhost_msgs from devhosts
mx_status_t devmgr_handler(mx_handle_t h, void* cb, void* cookie) {
    devhost_t* dh = cookie;
    devhost_msg_t msg;
    mx_handle_t hnd;
    mx_status_t r;

    if (h == 0) {
        devhost_remote_died(dh);
        return NO_ERROR;
    }

    uint32_t dsz = sizeof(msg);
    uint32_t hcount = 1;
    if ((r = _magenta_message_read(h, &msg, &dsz, &hnd, &hcount, 0)) < 0) {
        return r;
    }
    if (dsz != sizeof(msg)) {
        goto fail;
    }
    switch (msg.op) {
    case DH_OP_ADD:
        if (hcount != 1) {
            goto fail;
        }
        DM_LOCK();
        msg.arg = devhost_remote_add(dh, &msg, hnd);
        DM_UNLOCK();
        break;
    case DH_OP_REMOVE:
        if (hcount != 0) {
            goto fail;
        }
        DM_LOCK();
        msg.arg = devhost_remote_remove(dh, &msg);
        DM_UNLOCK();
        break;
    default:
        goto fail;
    }
    msg.op = DH_OP_STATUS;
    if ((r = _magenta_message_write(h, &msg, sizeof(msg), NULL, 0, 0)) < 0) {
        return r;
    }
    return NO_ERROR;
fail:
    printf("devmgr_handler: error %d\n", r);
    if (hcount) {
        _magenta_handle_close(hnd);
    }
    return ERR_IO;
}

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

static mx_status_t rio_handler(mx_rio_msg_t* msg, void* cookie) {
    mx_device_t* dev = cookie;
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    for (unsigned i = 0; i < msg->hcount; i++) {
        _magenta_handle_close(msg->handle[i]);
    }

    switch (MX_RIO_OP(msg->op)) {
    case MX_RIO_CLOSE:
        return NO_ERROR;
    case MX_RIO_CLONE: {
        mx_handle_t h0, h1;
        if ((h0 = _magenta_message_pipe_create(&h1)) < 0) {
            return h0;
        }
        msg->handle[0] = h0;
        if (dev->event > 0) {
            if ((msg->handle[1] = _magenta_handle_duplicate(dev->event)) < 0) {
                _magenta_handle_close(h0);
                _magenta_handle_close(h1);
                return msg->handle[1];
            }
            msg->hcount = 2;
        } else {
            msg->hcount = 1;
        }
        mxio_dispatcher_add(devmgr_dispatcher, h1, rio_handler, dev);
        msg->arg2.protocol = MXIO_PROTOCOL_REMOTE;
        return NO_ERROR;
    }
    case MX_RIO_READ: {
        mx_status_t r;
        mx_protocol_char_t* proto;
        if ((r = device_get_protocol(dev, MX_PROTOCOL_CHAR, (void**)&proto)) < 0) {
            return r;
        }
        if ((r = proto->read(dev, msg->data, arg)) > 0) {
            msg->datalen = r;
        }
        msg->arg2.off = 0;
        return r;
    }
    case MX_RIO_WRITE: {
        mx_status_t r;
        mx_protocol_char_t* proto;
        if ((r = device_get_protocol(dev, MX_PROTOCOL_CHAR, (void**)&proto)) < 0) {
            return r;
        }
        if ((r = proto->write(dev, msg->data, len)) > 0) {
            msg->arg2.off = 0;
        }
        return r;
    }
    case MX_RIO_IOCTL: {
        mx_status_t r;
        mx_protocol_char_t* proto;
        if (len > MXIO_IOCTL_MAX_INPUT || arg > (ssize_t)sizeof(msg->data)) {
            return ERR_INVALID_ARGS;
        }
        if ((r = device_get_protocol(dev, MX_PROTOCOL_CHAR, (void**)&proto)) < 0) {
            return r;
        }
        if (!proto->ioctl) {
            return ERR_NOT_SUPPORTED;
        }

        char in_buf[MXIO_IOCTL_MAX_INPUT];
        memcpy(in_buf, msg->data, len);
        if ((r = proto->ioctl(dev, msg->arg2.op, in_buf, len, msg->data, arg)) > 0) {
            msg->datalen = r;
        }
        msg->arg2.off = 0;
        return r;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

mx_status_t devhost_add(mx_device_t* dev, mx_device_t* parent) {
    mx_handle_t h0, h1;
    if ((h0 = _magenta_message_pipe_create(&h1)) < 0) {
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
        mxio_dispatcher_add(devmgr_dispatcher, h0, rio_handler, dev);
        dev->remote_id = msg.device_id;
    } else {
        _magenta_handle_close(h0);
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

mx_status_t devmgr_host_process(mx_device_t* dev, mx_driver_t* drv) {
#if SINGLE_PROCESS
    return ERR_NOT_SUPPORTED;
#else
    if (devmgr_is_remote) {
        return ERR_NOT_SUPPORTED;
    }
    // pci drivers get their own host process
    int index = devmgr_get_pcidev_index(dev);
    if (index < 0) {
        return ERR_NOT_SUPPORTED;
    }

    devhost_t* dh = calloc(1, sizeof(devhost_t));
    if (dh == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_handle_t h0, h1;
    if ((h0 = _magenta_message_pipe_create(&h1)) < 0) {
        free(dh);
        return h0;
    }

    dh->root = dev;
    dh->handle = h0;
    list_initialize(&dh->devices);
    list_add_tail(&devhost_list, &dh->node);
    mxio_dispatcher_add(devmgr_dispatcher, h0, NULL, dh);

    char arg0[32];
    char arg1[32];
    snprintf(arg0, 32, "pci=%d", index);
    snprintf(arg1, 32, "%p", drv);
    devmgr_launch_devhost(h1, arg0, arg1);
    //TODO: make drv ineligible for further probing?
    return 0;
#endif
}
