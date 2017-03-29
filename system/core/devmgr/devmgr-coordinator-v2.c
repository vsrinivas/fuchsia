// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include "devcoordinator.h"

static mx_handle_t devhost_job;
static port_t dc_port;
static list_node_t driver_list = LIST_INITIAL_VALUE(driver_list);
static device_ctx_t root_device;

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

static mx_status_t dc_handle_device_read(device_ctx_t* dc) {
    dc_msg_t msg;
    mx_handle_t hin[2];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = 2;

    mx_status_t r;
    if ((r = mx_channel_read(dc->hdevice, 0, &msg, msize, &msize,
                             hin, hcount, &hcount)) < 0) {
        return r;
    }

    const void* data;
    const char* name;
    const char* args;
    if ((r = dc_msg_unpack(&msg, msize, &data, &name, &args)) < 0) {
        goto fail;
    }

    switch (msg.op) {
    case DC_OP_ADD_DEVICE:
        printf("devcoord: add device '%s'\n", name);
        return NO_ERROR;

    case DC_OP_REMOVE_DEVICE:
        printf("devcoord: remove device '%s'\n", name);
        return NO_ERROR;

    default:
        printf("devcoord: invalid rpc op %08x\n", msg.op);
        r = ERR_NOT_SUPPORTED;
    }

fail:
    while (hcount > 0) {
        mx_handle_close(hin[--hcount]);
    }
    return r;
}

#define dctx_from_ph(ph) containerof(ph, device_ctx_t, ph)

mx_status_t dc_handle_device(port_handler_t* ph, mx_signals_t signals) {
    device_ctx_t* dctx = dctx_from_ph(ph);

    if (signals & MX_CHANNEL_READABLE) {
        mx_status_t r = dc_handle_device_read(dctx);
        if (r != NO_ERROR) {
            free(ph);
        }
        return r;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        printf("devcoord: device disconnected!\n");
        return ERR_REMOTE_CLOSED;
    }
    printf("devcoord: no work? %08x\n", signals);
    return NO_ERROR;
}


void coordinator_new_driver(driver_ctx_t* ctx) {
    printf("driver: %s @ %s\n", ctx->drv.name, ctx->libname);
    list_add_tail(&driver_list, &ctx->node);
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


void dh_create_device(devhost_ctx_t* dh, const char* name) {
    dc_msg_t msg;
    uint32_t mlen;

    mx_status_t r = dc_msg_pack(&msg, &mlen, NULL, 0, name, NULL);
    if (r) {
        printf("msgpack: %d\n", r);
        return;
    }

    msg.txid = 0;
    msg.op = DC_OP_CREATE_DEVICE;
    msg.protocol_id = 0;

    mx_channel_write(dh->hrpc, 0, &msg, mlen, NULL, 0);
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
    printf("coordinator()\n");
    acpi_init();
    enumerate_drivers();

    device_ctx_t* ctx = calloc(1, sizeof(device_ctx_t));
    memcpy(ctx->name, "misc", 5);
    do_publish(&root_device, ctx);

    devhost_ctx_t* dh;
    if (dc_new_devhost("devhost:misc", &dh) == NO_ERROR) {
        dh_create_device(dh, "misc");
    }

    mx_status_t status = port_dispatch(&dc_port);
    printf("coordinator: port dispatch ended: %d\n", status);
}