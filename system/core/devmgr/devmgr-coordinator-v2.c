// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <ddk/driver.h>
#include "devcoordinator.h"

static mx_status_t dc_handle_device(port_handler_t* ph, mx_signals_t signals);

static mx_handle_t devhost_job;
static port_t dc_port;
static list_node_t driver_list = LIST_INITIAL_VALUE(driver_list);

static device_ctx_t root_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV | DEV_CTX_MULTI_BIND,
    .name = "root",
};
static device_ctx_t misc_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV | DEV_CTX_MULTI_BIND,
    .protocol_id = MX_PROTOCOL_MISC_PARENT,
    .name = "misc",
};

static const char* devhost_bin = "/boot/bin/devhost2";

static mx_status_t dc_launch_devhost(const char* name, mx_handle_t hrpc) {
    launchpad_t* lp;
    launchpad_create(devhost_job, name, &lp);
    launchpad_load_from_file(lp, devhost_bin);
    launchpad_set_args(lp, 1, &devhost_bin);

    launchpad_add_handle(lp, hrpc, MX_HND_INFO(MX_HND_TYPE_USER0, 0));

    mx_handle_t h;
    mx_handle_duplicate(get_root_resource(), MX_RIGHT_SAME_RIGHTS, &h);
    launchpad_add_handle(lp, h, MX_HND_INFO(MX_HND_TYPE_RESOURCE, 0));

    launchpad_clone(lp, LP_CLONE_ENVIRON);

    //TODO: eventually devhosts should not have vfs access
    launchpad_add_handle(lp, vfs_create_global_root_handle(),
                         MX_HND_INFO(MX_HND_TYPE_MXIO_ROOT, 0));

    // Inherit devmgr's environment (including kernel cmdline)
    launchpad_clone(lp, LP_CLONE_ENVIRON | LP_CLONE_MXIO_ROOT);

    printf("devmgr: launch devhost: %s\n", name);
    const char* errmsg;
    mx_status_t status = launchpad_go(lp, NULL, &errmsg);
    if (status < 0) {
        printf("devmgr: launch devhost: %s: failed: %d: %s\n",
               name, status, errmsg);
        return status;
    }

    return NO_ERROR;
}

static mx_status_t dc_new_devhost(const char* name, devhost_ctx_t** out) {
    devhost_ctx_t* ctx = calloc(1, sizeof(devhost_ctx_t));
    if (ctx == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_handle_t hrpc;
    mx_status_t r;
    if ((r = mx_channel_create(0, &hrpc, &ctx->hrpc)) < 0) {
        free(ctx);
        return r;
    }

    if ((r = dc_launch_devhost(name, hrpc)) < 0) {
        mx_handle_close(ctx->hrpc);
        free(ctx);
        return r;
    }

    *out = ctx;
    return NO_ERROR;
}

// Add a new device to a parent device (same devhost)
// New device is published in devfs.
static mx_status_t dc_add_device(device_ctx_t* parent, mx_handle_t hdevice,
                                 dc_msg_t* msg, const char* name,
                                 const char* args, const void* data) {
    if (msg->namelen >= MX_DEVICE_NAME_MAX) {
        return ERR_INVALID_ARGS;
    }
    device_ctx_t* dev;
    if ((dev = calloc(1, sizeof(*dev) + msg->argslen + 1)) == NULL) {
        return ERR_NO_MEMORY;
    }
    dev->hdevice = hdevice;
    dev->host = parent->host;
    dev->args = (const char*) (dev + 1);
    memcpy((char*) (dev + 1), args, msg->argslen + 1);
    memcpy(dev->name, name, msg->namelen + 1);
    dev->protocol_id = msg->protocol_id;

    mx_status_t r;
    if ((r = do_publish(parent, dev)) < 0) {
        free(dev);
        return r;
    }

    dev->ph.handle = hdevice;
    dev->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    dev->ph.func = dc_handle_device;
    if ((r = port_watch(&dc_port, &dev->ph)) < 0) {
        do_unpublish(dev);
        free(dev);
        return r;
    }

    return NO_ERROR;
}

// Remove device from parent
static mx_status_t dc_remove_device(device_ctx_t* dev) {
    if (dev->flags & DEV_CTX_IMMORTAL) {
        printf("devcoord: cannot remove dev %p (immortal)\n", dev);
    } else {
        do_unpublish(dev);
        dev->flags |= DEV_CTX_DEAD;
    }
    return NO_ERROR;
}

static mx_status_t dc_handle_device_read(device_ctx_t* dev) {
    dc_msg_t msg;
    mx_handle_t hin[2];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 2;

    if (dev->flags & DEV_CTX_DEAD) {
        printf("devcoord: dev %p already dead\n", dev);
        return ERR_INTERNAL;
    }

    mx_status_t r;
    if ((r = mx_channel_read(dev->hdevice, 0, &msg, msize, &msize,
                             hin, hcount, &hcount)) < 0) {
        return r;
    }

    const void* data;
    const char* name;
    const char* args;
    if ((r = dc_msg_unpack(&msg, msize, &data, &name, &args)) < 0) {
        return ERR_INTERNAL;
    }

    switch (msg.op) {
    case DC_OP_ADD_DEVICE:
        if (hcount != 1) {
            r = ERR_INVALID_ARGS;
            goto fail;
        }
        printf("devcoord: add device '%s'\n", name);
        if ((r = dc_add_device(dev, hin[0], &msg, name, args, data)) == NO_ERROR) {
            goto done;
        }
        break;

    case DC_OP_REMOVE_DEVICE:
        if (hcount != 0) {
            r = ERR_INVALID_ARGS;
            goto fail;
        }
        printf("devcoord: remove device '%s'\n", name);
        if ((r = dc_remove_device(dev)) == NO_ERROR) {
            goto done;
        }
        break;

    default:
        printf("devcoord: invalid rpc op %08x\n", msg.op);
        r = ERR_NOT_SUPPORTED;
        break;
    }

fail:
    while (hcount > 0) {
        mx_handle_close(hin[--hcount]);
    }
done:
    ;
    dc_status_t dcs = {
        .txid = msg.txid,
        .status = r,
    };
    if ((r = mx_channel_write(dev->hdevice, 0, &dcs, sizeof(dcs), NULL, 0)) < 0) {
        return r;
    }
    return NO_ERROR;
}

void dc_destroy_device(device_ctx_t* dev) {
    if (dev->flags & DEV_CTX_IMMORTAL) {
        printf("devcoord: cannot destroy dev %p (immortal)\n", dev);
        return;
    }
    if (!(dev->flags & DEV_CTX_DEAD)) {
        dc_remove_device(dev);
    }
    free(dev);
}

#define dev_from_ph(ph) containerof(ph, device_ctx_t, ph)

// handle inbound RPCs from devhost to devices
static mx_status_t dc_handle_device(port_handler_t* ph, mx_signals_t signals) {
    device_ctx_t* dev = dev_from_ph(ph);

    if (signals & MX_CHANNEL_READABLE) {
        mx_status_t r = dc_handle_device_read(dev);
        if (r != NO_ERROR) {
            dc_destroy_device(dev);
        }
        return r;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        printf("devcoord: device disconnected!\n");
        dc_destroy_device(dev);
        return ERR_PEER_CLOSED;
    }
    printf("devcoord: no work? %08x\n", signals);
    return NO_ERROR;
}

// send message to devhost, requesting the creation of a device
static mx_status_t dh_create_device(device_ctx_t* dev, devhost_ctx_t* dh) {
    dc_msg_t msg;
    uint32_t mlen;

    mx_status_t r;

    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, dev->name, NULL)) < 0) {
        return r;
    }

    mx_handle_t h0, h1;
    if ((r = mx_channel_create(0, &h0, &h1)) < 0) {
        return r;
    }

    msg.txid = 0;
    msg.op = DC_OP_CREATE_DEVICE;
    msg.protocol_id = dev->protocol_id;

    if ((r = mx_channel_write(dh->hrpc, 0, &msg, mlen, &h1, 1)) < 0) {
        mx_handle_close(h0);
        mx_handle_close(h1);
        return r;
    }

    dev->hdevice = h0;
    dev->ph.handle = h0;
    dev->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    dev->ph.func = dc_handle_device;
    if ((r = port_watch(&dc_port, &dev->ph)) < 0) {
        mx_handle_close(h0);
        return r;
    }

    return NO_ERROR;
}

// send message to devhost, requesting the binding of a driver to a device
static mx_status_t dh_bind_driver(device_ctx_t* dev, const char* libname) {
    dc_msg_t msg;
    uint32_t mlen;

    mx_status_t r;

    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, libname, NULL)) < 0) {
        return r;
    }

    msg.txid = 0;
    msg.op = DC_OP_BIND_DRIVER;

    if ((r = mx_channel_write(dev->hdevice, 0, &msg, mlen, NULL, 0)) < 0) {
        return r;
    }

    return NO_ERROR;
}

static void dc_attempt_bind(driver_ctx_t* drv, device_ctx_t* dev) {
    // cannot bind driver to already bound device
    if (dev->flags & DEV_CTX_BOUND) {
        return;
    }

    // if this device has no devhost, first instantiate it
    if (dev->host == NULL) {
        mx_status_t r;
        if ((r = dc_new_devhost("devhost:misc", &dev->host)) < 0) {
            printf("devmgr: dh_new_devhost: %d\n", r);
            return;
        }
        if ((r = dh_create_device(dev, dev->host)) < 0) {
            printf("devmgr: dh_create_device: %d\n", r);
            return;
        }
    }

    dh_bind_driver(dev, drv->libname);
}

// device binding program that pure (parentless)
// misc devices use to get published in the
// primary devhost
static struct mx_bind_inst misc_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT);

static bool is_misc_driver(mx_driver_t* drv) {
    return (drv->binding_size == sizeof(misc_device_binding)) &&
        (memcmp(&misc_device_binding, drv->binding, sizeof(misc_device_binding)) == 0);
}

void coordinator_new_driver(driver_ctx_t* ctx) {
    //printf("driver: %s @ %s\n", ctx->drv.name, ctx->libname);
    list_add_tail(&driver_list, &ctx->node);

    if (is_misc_driver(&ctx->drv)) {
        printf("driver: %s @ %s is MISC\n", ctx->drv.name, ctx->libname);
        dc_attempt_bind(ctx, &misc_device);
    }
}

void coordinator_init(VnodeDir* vnroot, mx_handle_t root_job) {
    printf("coordinator_init()\n");

    mx_status_t status = mx_job_create(root_job, 0u, &devhost_job);
    if (status < 0) {
        printf("unable to create devhost job\n");
    }
    mx_object_set_property(devhost_job, MX_PROP_NAME, "magenta-drivers", 15);

    root_device.vnode = vnroot;

    port_init(&dc_port);
}

//TODO: The acpisvc needs to become the acpi bus device
//      For now, we launch it manually here so PCI can work
#include "acpi.h"

static void acpi_init(void) {
    mx_status_t status = devhost_launch_acpisvc(devhost_job);
    if (status != NO_ERROR) {
        return;
    }

    // Ignore the return value of this; if it fails, it may just be that the
    // platform doesn't support initing PCIe via ACPI.  If the platform needed
    // it, it will fail later.
    devhost_init_pcie();
}

void coordinator(void) {
    printf("devmgr: coordinator()\n");
    acpi_init();

    do_publish(&root_device, &misc_device);

    enumerate_drivers();

    mx_status_t status = port_dispatch(&dc_port);
    printf("coordinator: port dispatch ended: %d\n", status);
}
