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

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <system/listnode.h>

#define MXDEBUG 0

static mx_driver_t proxy_driver = {
    .name = "proxy",
};

static list_node_t devhost_list = LIST_INITIAL_VALUE(devhost_list);

typedef struct devhost devhost_t;
typedef struct proxy proxy_t;

struct proxy {
    mx_device_t device;
    list_node_t node;
};

static mx_status_t proxy_release(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t proxy_device_proto = {
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
    xprintf("devmgr: remote %p added dev %p name '%s'\n",
            dh, &proxy->device, proxy->device.name);
    return NO_ERROR;
fail1:
    free(proxy);
fail0:
    mx_handle_close(h);
    return r;
}

static mx_status_t devhost_remote_remove(devhost_t* dh, devhost_msg_t* msg) {
    mx_device_t* dev = devhost_id_to_dev(dh, msg->device_id);
    printf("devmgr: remote %p remove %p: dev=%p\n", dh, (void*)msg->device_id, dev);
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
    if ((r = mx_message_read(h, &msg, &dsz, &hnd, &hcount, 0)) < 0) {
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
    if ((r = mx_message_write(h, &msg, sizeof(msg), NULL, 0, 0)) < 0) {
        return r;
    }
    return NO_ERROR;
fail:
    printf("devmgr_handler: error %d\n", r);
    if (hcount) {
        mx_handle_close(hnd);
    }
    return ERR_IO;
}

mx_status_t devmgr_host_process(mx_device_t* dev, mx_driver_t* drv) {
#if LIBDRIVER
    return ERR_NOT_SUPPORTED;
#else
    if (devmgr_is_remote) {
        return ERR_NOT_SUPPORTED;
    }
    // pci drivers get their own host process
    uint16_t vid, did;
    int index = devmgr_get_pcidev_index(dev, &vid, &did);
    if (index < 0) {
        return ERR_NOT_SUPPORTED;
    }

    devhost_t* dh = calloc(1, sizeof(devhost_t));
    if (dh == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_handle_t h0, h1;
    if ((h0 = mx_message_pipe_create(&h1)) < 0) {
        free(dh);
        return h0;
    }

    dh->root = dev;
    dh->handle = h0;
    list_initialize(&dh->devices);
    list_add_tail(&devhost_list, &dh->node);
    mxio_dispatcher_add(devmgr_devhost_dispatcher, h0, NULL, dh);

    char name[64];
    char arg0[32];
    char arg1[32];

    if (drv == NULL) {
        // check for a specific driver binary for this device
        snprintf(name, 64, "/boot/bin/driver-pci-%04x-%04x", vid, did);
        struct stat s;
        if (stat(name, &s)) {
            return ERR_NOT_FOUND;
        }
    } else {
        // otherwise launch a devhost
        snprintf(name, 64, "devhost:pci:%d", index);
    }
    printf("devmgr: remote(%p) for '%s'\n", dh, name);
    snprintf(arg0, 32, "pci=%d", index);
    snprintf(arg1, 32, "%p", drv);
    devmgr_launch_devhost(name, h1, arg0, arg1);

    //TODO: make drv ineligible for further probing?
    return 0;
#endif
}
