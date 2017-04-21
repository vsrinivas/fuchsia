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
#include "log.h"

uint32_t log_flags = LOG_ERROR | LOG_INFO;

static mx_status_t dc_handle_device(port_handler_t* ph, mx_signals_t signals);

static mx_handle_t devhost_job;
static port_t dc_port;
static list_node_t list_drivers = LIST_INITIAL_VALUE(list_drivers);

static device_t root_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV | DEV_CTX_MULTI_BIND,
    .name = "root",
};
static device_t misc_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV | DEV_CTX_MULTI_BIND,
    .protocol_id = MX_PROTOCOL_MISC_PARENT,
    .name = "misc",
};

static void dc_handle_new_device(device_t* dev);

#define WORK_IDLE 0
#define WORK_DEVICE_ADDED 1
static list_node_t list_pending_work = LIST_INITIAL_VALUE(list_pending_work);
static list_node_t list_unbound_devices = LIST_INITIAL_VALUE(list_unbound_devices);

static inline void queue_work(work_t* work, uint32_t op, uint32_t arg) {
    MX_ASSERT(work->op == WORK_IDLE);
    work->op = op;
    work->arg = arg;
    list_add_tail(&list_pending_work, &work->node);
}

static void process_work(work_t* work) {
    uint32_t op = work->op;
    work->op = WORK_IDLE;

    switch (op) {
    case WORK_DEVICE_ADDED: {
        device_t* dev = containerof(work, device_t, work);
        dc_handle_new_device(dev);
        break;
    }
    default:
        log(ERROR, "devcoord: unknown work: op=%u\n", op);
    }
}

static const char* devhost_bin = "/boot/bin/devhost2";

static mx_status_t dc_launch_devhost(devhost_t* host,
                                     const char* name, mx_handle_t hrpc) {
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

    const char* errmsg;
    mx_status_t status = launchpad_go(lp, &host->proc, &errmsg);
    if (status < 0) {
        log(ERROR, "devcoord: launch devhost '%s': failed: %d: %s\n",
            name, status, errmsg);
        return status;
    }
    mx_info_handle_basic_t info;
    if (mx_object_get_info(host->proc, MX_INFO_HANDLE_BASIC, &info,
                           sizeof(info), NULL, NULL) == NO_ERROR) {
        host->koid = info.koid;
    }
    log(INFO, "devcoord: launch devhost '%s': pid=%zu\n",
        name, host->koid);

    return NO_ERROR;
}

static mx_status_t dc_new_devhost(const char* name, devhost_t** out) {
    devhost_t* ctx = calloc(1, sizeof(devhost_t));
    if (ctx == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_handle_t hrpc;
    mx_status_t r;
    if ((r = mx_channel_create(0, &hrpc, &ctx->hrpc)) < 0) {
        free(ctx);
        return r;
    }

    if ((r = dc_launch_devhost(ctx, name, hrpc)) < 0) {
        mx_handle_close(ctx->hrpc);
        free(ctx);
        return r;
    }

    *out = ctx;
    return NO_ERROR;
}

// Add a new device to a parent device (same devhost)
// New device is published in devfs.
// Caller closes handles on error, so we don't have to.
static mx_status_t dc_add_device(device_t* parent,
                                 mx_handle_t* handle, size_t hcount,
                                 dc_msg_t* msg, const char* name,
                                 const char* args, const void* data) {
    if (hcount == 0) {
        return ERR_INVALID_ARGS;
    }
    if (msg->namelen >= MX_DEVICE_NAME_MAX) {
        return ERR_INVALID_ARGS;
    }
    if (msg->datalen % sizeof(mx_device_prop_t)) {
        return ERR_INVALID_ARGS;
    }
    device_t* dev;
    // allocate device struct, followed by space for props, followed
    // by space for bus arguments
    if ((dev = calloc(1, sizeof(*dev) + msg->datalen + msg->argslen + 1)) == NULL) {
        return ERR_NO_MEMORY;
    }
    dev->hrpc = handle[0];
    dev->hrsrc = (hcount > 1) ? handle[1] : MX_HANDLE_INVALID;
    dev->prop_count = msg->datalen / sizeof(mx_device_prop_t);
    dev->args = (const char*) (dev->props + dev->prop_count);
    memcpy(dev->props, data, msg->datalen);
    memcpy((char*) (dev->props + dev->prop_count), args, msg->argslen + 1);
    memcpy(dev->name, name, msg->namelen + 1);
    dev->protocol_id = msg->protocol_id;

    // If we have bus device args or resource handle
    // we are, by definition a bus device.
    if (args[0] || (dev->hrsrc != MX_HANDLE_INVALID)) {
        dev->flags |= DEV_CTX_BUSDEV;
    } else {
        //TODO: create shadow instead
        dev->host = parent->host;
    }

    mx_status_t r;
    if ((r = do_publish(parent, dev)) < 0) {
        free(dev);
        return r;
    }

    dev->ph.handle = handle[0];
    dev->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    dev->ph.func = dc_handle_device;
    if ((r = port_watch(&dc_port, &dev->ph)) < 0) {
        do_unpublish(dev);
        free(dev);
        return r;
    }

    log(DEVFS, "devcoord: publish '%s' props=%u args='%s'\n",
        dev->name, dev->prop_count, dev->args);

    queue_work(&dev->work, WORK_DEVICE_ADDED, 0);
    return NO_ERROR;
}

// Remove device from parent
static mx_status_t dc_remove_device(device_t* dev) {
    if (dev->flags & DEV_CTX_IMMORTAL) {
        log(ERROR, "devcoord: cannot remove dev %p (immortal)\n", dev);
    } else {
        do_unpublish(dev);
        dev->flags |= DEV_CTX_DEAD;
    }
    return NO_ERROR;
}

static mx_status_t dc_handle_device_read(device_t* dev) {
    dc_msg_t msg;
    mx_handle_t hin[2];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 2;

    if (dev->flags & DEV_CTX_DEAD) {
        log(ERROR, "devcoord: dev %p already dead (in read)\n", dev);
        return ERR_INTERNAL;
    }

    mx_status_t r;
    if ((r = mx_channel_read(dev->hrpc, 0, &msg, msize, &msize,
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
        log(RPC_IN, "devcoord: add device '%s' args='%s'\n", name, args);
        if ((r = dc_add_device(dev, hin, hcount, &msg, name, args, data)) == NO_ERROR) {
            goto done;
        }
        break;

    case DC_OP_REMOVE_DEVICE:
        if (hcount != 0) {
            r = ERR_INVALID_ARGS;
            goto fail;
        }
        log(RPC_IN, "devcoord: remove device '%s'\n", name);
        if ((r = dc_remove_device(dev)) == NO_ERROR) {
            goto done;
        }
        break;

    default:
        log(ERROR, "devcoord: invalid rpc op %08x\n", msg.op);
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
    if ((r = mx_channel_write(dev->hrpc, 0, &dcs, sizeof(dcs), NULL, 0)) < 0) {
        return r;
    }
    return NO_ERROR;
}

void dc_destroy_device(device_t* dev) {
    if (dev->flags & DEV_CTX_IMMORTAL) {
        log(ERROR, "devcoord: cannot destroy dev %p (immortal)\n", dev);
        return;
    }
    if (!(dev->flags & DEV_CTX_DEAD)) {
        dc_remove_device(dev);
    }
    free(dev);
}

#define dev_from_ph(ph) containerof(ph, device_t, ph)

// handle inbound RPCs from devhost to devices
static mx_status_t dc_handle_device(port_handler_t* ph, mx_signals_t signals) {
    device_t* dev = dev_from_ph(ph);

    if (signals & MX_CHANNEL_READABLE) {
        mx_status_t r = dc_handle_device_read(dev);
        if (r != NO_ERROR) {
            dc_destroy_device(dev);
        }
        return r;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        log(ERROR, "devcoord: device disconnected!\n");
        dc_destroy_device(dev);
        return ERR_PEER_CLOSED;
    }
    log(ERROR, "devcoord: no work? %08x\n", signals);
    return NO_ERROR;
}

// send message to devhost, requesting the creation of a device
static mx_status_t dh_create_device(device_t* dev, devhost_t* dh,
                                    const char* libname) {
    dc_msg_t msg;
    uint32_t mlen;
    mx_status_t r;
    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, libname, dev->args)) < 0) {
        return r;
    }

    mx_handle_t handle[2], hrpc;
    if ((r = mx_channel_create(0, handle, &hrpc)) < 0) {
        return r;
    }

    if (dev->hrsrc != MX_HANDLE_INVALID) {
        if ((r = mx_handle_duplicate(dev->hrsrc, MX_RIGHT_SAME_RIGHTS, handle + 1)) < 0) {
            goto fail_duplicate;
        }
    }

    msg.txid = 0;
    msg.op = DC_OP_CREATE_DEVICE;
    msg.protocol_id = dev->protocol_id;

    if ((r = mx_channel_write(dh->hrpc, 0, &msg, mlen, handle,
                              (dev->hrsrc != MX_HANDLE_INVALID) ? 2 : 1)) < 0) {
        goto fail_write;
    }

    dev->hrpc = hrpc;
    dev->ph.handle = hrpc;
    dev->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    dev->ph.func = dc_handle_device;
    if ((r = port_watch(&dc_port, &dev->ph)) < 0) {
        goto fail_watch;
    }
    return NO_ERROR;

fail_write:
    if (dev->hrsrc != MX_HANDLE_INVALID) {
        mx_handle_close(handle[1]);
    }
fail_duplicate:
    mx_handle_close(handle[0]);
fail_watch:
    mx_handle_close(hrpc);
    return r;
}

// send message to devhost, requesting the binding of a driver to a device
static mx_status_t dh_bind_driver(device_t* dev, const char* libname) {
    dc_msg_t msg;
    uint32_t mlen;

    mx_status_t r;

    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, libname, NULL)) < 0) {
        return r;
    }

    msg.txid = 0;
    msg.op = DC_OP_BIND_DRIVER;

    if ((r = mx_channel_write(dev->hrpc, 0, &msg, mlen, NULL, 0)) < 0) {
        return r;
    }

    return NO_ERROR;
}

static void dc_attempt_bind(driver_ctx_t* drv, device_t* dev,
                            const char* devhostname, const char* libname) {
    // cannot bind driver to already bound device
    if (dev->flags & DEV_CTX_BOUND) {
        return;
    }
    if (!(dev->flags & DEV_CTX_BUSDEV)) {
        //TODO: non-busdev codepath
        log(ERROR, "devcoord: can't bind non-busdevs yet...\n");
        return;
    }

    // if this device has no devhost, first instantiate it
    if (dev->host == NULL) {
        mx_status_t r;
        if ((r = dc_new_devhost(devhostname, &dev->host)) < 0) {
            log(ERROR, "devcoord: dh_new_devhost: %d\n", r);
            return;
        }
        if ((r = dh_create_device(dev, dev->host, libname)) < 0) {
            log(ERROR, "devcoord: dh_create_device: %d\n", r);
            return;
        }
    }

    dh_bind_driver(dev, drv->libname);
}

static void dc_handle_new_device(device_t* dev) {
    driver_ctx_t* drv;

    list_for_every_entry(&list_drivers, drv, driver_ctx_t, node) {
        if (dc_is_bindable(&drv->drv, dev->protocol_id,
                           dev->props, dev->prop_count, true)) {
            log(INFO, "devcoord: drv='%s' bindable to dev='%s'\n",
                drv->drv.name, dev->name);
            if (dev->protocol_id == MX_PROTOCOL_PCI) {
                dc_attempt_bind(drv, dev, "devhost:pci", "driver/bus-pci.so");
            } else {
                log(ERROR, "devcoord: but that is not supported yet\n");
            }
            break;
        }
    }

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
    list_add_tail(&list_drivers, &ctx->node);

    if (!strcmp(ctx->drv.name, "pci")) {
        log(INFO, "driver: %s @ %s is PCI\n", ctx->drv.name, ctx->libname);
        dc_attempt_bind(ctx, &root_device, "devhost:pci", "");
        return;
    }
    if (is_misc_driver(&ctx->drv)) {
        log(INFO, "driver: %s @ %s is MISC\n", ctx->drv.name, ctx->libname);
        dc_attempt_bind(ctx, &misc_device, "devhost:misc", "");
        return;
    }
}

void coordinator_init(VnodeDir* vnroot, mx_handle_t root_job) {
    printf("coordinator_init()\n");

    mx_status_t status = mx_job_create(root_job, 0u, &devhost_job);
    if (status < 0) {
        log(ERROR, "devcoord: unable to create devhost job\n");
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
    log(INFO, "devmgr: coordinator()\n");
    acpi_init();

    do_publish(&root_device, &misc_device);

    enumerate_drivers();

    for (;;) {
        mx_status_t status;
        if (list_is_empty(&list_pending_work)) {
            status = port_dispatch(&dc_port, MX_TIME_INFINITE);
        } else {
            status = port_dispatch(&dc_port, 0);
            if (status == ERR_TIMED_OUT) {
                process_work(list_remove_head_type(&list_pending_work, work_t, node));
                continue;
            }
        }
        if (status != NO_ERROR) {
            log(ERROR, "devcoord: port dispatch ended: %d\n", status);
        }
    }
}
