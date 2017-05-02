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

//TODO: these are copied from devhost.h
#define ID_HJOBROOT 4
mx_handle_t get_sysinfo_job_root(void);


uint32_t log_flags = LOG_ERROR | LOG_INFO;

static mx_status_t dc_handle_device(port_handler_t* ph, mx_signals_t signals);

static mx_handle_t devhost_job;
static port_t dc_port;
static list_node_t list_drivers = LIST_INITIAL_VALUE(list_drivers);

static device_t root_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV | DEV_CTX_MULTI_BIND,
    .name = "root",
    .children = LIST_INITIAL_VALUE(root_device.children),
    .pending = LIST_INITIAL_VALUE(root_device.pending),
    .refcount = 1,
};

static device_t misc_device = {
    .flags = DEV_CTX_IMMORTAL | DEV_CTX_BUSDEV | DEV_CTX_MULTI_BIND,
    .protocol_id = MX_PROTOCOL_MISC_PARENT,
    .name = "misc",
    .children = LIST_INITIAL_VALUE(misc_device.children),
    .pending = LIST_INITIAL_VALUE(misc_device.pending),
    .refcount = 1,
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

    launchpad_add_handle(lp, hrpc, PA_HND(PA_USER0, 0));

    mx_handle_t h;
    //TODO: limit root resource to root devhost only
    mx_handle_duplicate(get_root_resource(), MX_RIGHT_SAME_RIGHTS, &h);
    launchpad_add_handle(lp, h, PA_HND(PA_RESOURCE, 0));

    launchpad_clone(lp, LP_CLONE_ENVIRON);

    //TODO: eventually devhosts should not have vfs access
    launchpad_add_handle(lp, vfs_create_global_root_handle(),
                         PA_HND(PA_MXIO_ROOT, 0));

    //TODO: limit root job access to root devhost only
    launchpad_add_handle(lp, get_sysinfo_job_root(),
                         PA_HND(PA_USER0, ID_HJOBROOT));

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

// called when device children or shadows are removed
static void dc_release_device(device_t* dev) {
    dev->refcount--;
    if (dev->refcount > 0) {
        return;
    }

    // Immortal devices are never destroyed
    if (dev->flags & DEV_CTX_IMMORTAL) {
        return;
    }

    log(INFO, "devcoord: dev %p (name='%s') destroyed\n", dev, dev->name);

    do_unpublish(dev);

    if (dev->hrpc) {
        mx_handle_close(dev->hrpc);
        dev->hrpc = MX_HANDLE_INVALID;
        dev->ph.handle = MX_HANDLE_INVALID;
    }
    if (dev->hrsrc) {
        mx_handle_close(dev->hrsrc);
        dev->hrsrc = MX_HANDLE_INVALID;
    }
    dev->host = NULL;
    //TODO: refcount, reap hosts
    free(dev);
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
    list_initialize(&dev->children);
    list_initialize(&dev->pending);
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
    }

    // We exist within our parent's device host
    dev->host = parent->host;

    // If our parent is a shadow, for the purpose
    // of devicefs, we need to work with *its* parent
    // which is the device that it is shadowing.
    if (parent->flags & DEV_CTX_SHADOW) {
        parent = parent->parent;
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

    list_add_tail(&parent->children, &dev->node);
    parent->refcount++;

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
        printf("REMOVE %p %s\n", dev, dev->name);
        dev->flags |= DEV_CTX_DEAD;

        // remove from devfs, preventing further OPEN attempts
        do_unpublish(dev);

        // if we have a parent, disconnect and downref it
        device_t* parent = dev->parent;
        if (parent != NULL) {
            if (dev->flags & DEV_CTX_SHADOW) {
                parent->shadow = NULL;
            } else {
                list_delete(&dev->node);
            }
            dev->parent = NULL;
            dc_release_device(dev->parent);
        }
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
    if ((r = mx_channel_read(dev->hrpc, 0, &msg, hin,
                             msize, hcount, &msize, &hcount)) < 0) {
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
        log(RPC_IN, "devcoord: rpc: add device '%s' args='%s'\n", name, args);
        if ((r = dc_add_device(dev, hin, hcount, &msg, name, args, data)) == NO_ERROR) {
            goto done;
        }
        break;

    case DC_OP_REMOVE_DEVICE:
        if (hcount != 0) {
            r = ERR_INVALID_ARGS;
            goto fail;
        }
        log(RPC_IN, "devcoord: rpc: remove device '%s'\n", dev->name);
        if ((r = dc_remove_device(dev)) == NO_ERROR) {
            goto done;
        }
        break;

    case DC_OP_STATUS:
        // all of these return directly and do not write a
        // reply, since this message is a reply itself
        if (hcount != 0) {
            while (hcount > 0) {
                mx_handle_close(hin[--hcount]);
                return NO_ERROR;
            }
        }
        pending_t* pending = list_remove_tail_type(&dev->pending, pending_t, node);
        if (pending == NULL) {
            log(ERROR, "devcoord: rpc: spurious status message\n");
            return NO_ERROR;
        }
        switch (pending->op) {
        case PENDING_BIND:
            if (msg.status != NO_ERROR) {
                log(ERROR, "devcoord: rpc: bind device '%s' status %d\n",
                    dev->name, msg.status);
            }
            //TODO: try next driver, clear BOUND flag
            break;
        }
        free(pending);
        return NO_ERROR;

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

    // Where to get information to send to devhost from?
    // Shadow devices defer to the device they're shadowing,
    // otherwise we use the information from the device itself.
    device_t* info = (dev->flags & DEV_CTX_SHADOW) ? dev->parent : dev;

    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, libname, info->args)) < 0) {
        return r;
    }

    mx_handle_t handle[2], hrpc;
    if ((r = mx_channel_create(0, handle, &hrpc)) < 0) {
        return r;
    }

    if (info->hrsrc != MX_HANDLE_INVALID) {
        if ((r = mx_handle_duplicate(info->hrsrc, MX_RIGHT_SAME_RIGHTS, handle + 1)) < 0) {
            goto fail_duplicate;
        }
    }

    msg.txid = 0;
    msg.op = DC_OP_CREATE_DEVICE;
    msg.protocol_id = dev->protocol_id;

    if ((r = mx_channel_write(dh->hrpc, 0, &msg, mlen, handle,
                              (info->hrsrc != MX_HANDLE_INVALID) ? 2 : 1)) < 0) {
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
    if (info->hrsrc != MX_HANDLE_INVALID) {
        mx_handle_close(handle[1]);
    }
fail_duplicate:
    mx_handle_close(handle[0]);
fail_watch:
    mx_handle_close(hrpc);
    return r;
}

static mx_status_t dc_create_shadow(device_t* parent) {
    if (parent->shadow != NULL) {
        return NO_ERROR;
    }

    device_t* dev = calloc(1, sizeof(mx_device_t));
    if (dev == NULL) {
        return ERR_NO_MEMORY;
    }
    memcpy(dev->name, parent->name, sizeof(dev->name));
    list_initialize(&dev->children);
    list_initialize(&dev->pending);
    dev->flags = DEV_CTX_SHADOW;
    dev->protocol_id = parent->protocol_id;
    dev->parent = parent;
    parent->shadow = dev;
    parent->refcount++;
    return NO_ERROR;
}

// send message to devhost, requesting the binding of a driver to a device
static mx_status_t dh_bind_driver(device_t* dev, const char* libname) {
    dc_msg_t msg;
    uint32_t mlen;

    pending_t* pending = malloc(sizeof(pending_t));
    if (pending == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_status_t r;
    if ((r = dc_msg_pack(&msg, &mlen, NULL, 0, libname, NULL)) < 0) {
        free(pending);
        return r;
    }

    msg.txid = 0;
    msg.op = DC_OP_BIND_DRIVER;

    if ((r = mx_channel_write(dev->hrpc, 0, &msg, mlen, NULL, 0)) < 0) {
        free(pending);
        return r;
    }

    dev->flags |= DEV_CTX_BOUND;
    pending->op = PENDING_BIND;
    pending->ctx = NULL;
    list_add_tail(&dev->pending, &pending->node);
    return NO_ERROR;
}

static mx_status_t dc_attempt_bind(driver_t* drv, device_t* dev) {
    // cannot bind driver to already bound device
    if ((dev->flags & DEV_CTX_BOUND) && (!(dev->flags & DEV_CTX_MULTI_BIND))) {
        return ERR_BAD_STATE;
    }
    if (!(dev->flags & DEV_CTX_BUSDEV)) {
        // non-busdev is pretty simple
        if (dev->host == NULL) {
            log(ERROR, "devcoord: can't bind to device without devhost\n");
            return ERR_BAD_STATE;
        }
        return dh_bind_driver(dev, drv->libname);
    }

    //TODO: generic discovery of driver for shadow devices
    const char* libname = "";
    const char* devhostname = "devhost";
    if (dev->protocol_id == MX_PROTOCOL_PCI) {
        libname = "driver/bus-pci.so";
        devhostname = "devhost:pci";
    } else if (dev->protocol_id == MX_PROTOCOL_MISC_PARENT) {
        libname = "";
        devhostname = "devhost:misc";
    } else if (dev == &root_device) {
        libname = "";
        devhostname = "devhost:root";
    } else {
        log(ERROR, "devcoord: cannot create proto %x shadow (yet)\n", dev->protocol_id);
        return ERR_NOT_SUPPORTED;
    }

    mx_status_t r;
    if ((r = dc_create_shadow(dev)) < 0) {
        log(ERROR, "devcoord: cannot create shadow device: %d\n", r);
        return r;
    }

    // if this device has no devhost, first instantiate it
    if (dev->shadow->host == NULL) {
        if ((r = dc_new_devhost(devhostname, &dev->shadow->host)) < 0) {
            log(ERROR, "devcoord: dh_new_devhost: %d\n", r);
            return r;
        }
        if ((r = dh_create_device(dev->shadow, dev->shadow->host, libname)) < 0) {
            log(ERROR, "devcoord: dh_create_device: %d\n", r);
            return r;
        }
    }

    return dh_bind_driver(dev->shadow, drv->libname);
}

static void dc_handle_new_device(device_t* dev) {
    driver_t* drv;

    list_for_every_entry(&list_drivers, drv, driver_t, node) {
        if (dc_is_bindable(drv, dev->protocol_id,
                           dev->props, dev->prop_count, true)) {
            log(INFO, "devcoord: drv='%s' bindable to dev='%s'\n",
                drv->name, dev->name);

            dc_attempt_bind(drv, dev);
            break;
        }
    }

}

// device binding program that pure (parentless)
// misc devices use to get published in the
// primary devhost
static struct mx_bind_inst misc_device_binding =
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT);

static bool is_misc_driver(driver_t* drv) {
    return (drv->binding_size == sizeof(misc_device_binding)) &&
        (memcmp(&misc_device_binding, drv->binding, sizeof(misc_device_binding)) == 0);
}

void coordinator_new_driver(driver_t* ctx) {
    //printf("driver: %s @ %s\n", ctx->drv.name, ctx->libname);
    list_add_tail(&list_drivers, &ctx->node);

    if (!strcmp(ctx->name, "pci")) {
        log(INFO, "driver: %s @ %s is PCI\n", ctx->name, ctx->libname);
        dc_attempt_bind(ctx, &root_device);
        return;
    }
    if (is_misc_driver(ctx)) {
        log(INFO, "driver: %s @ %s is MISC\n", ctx->name, ctx->libname);
        dc_attempt_bind(ctx, &misc_device);
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

    // bind "built-in" root devices first
    driver_t drv = {
        .libname = "driver/root.so",
    };
    dc_attempt_bind(&drv, &root_device);

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
