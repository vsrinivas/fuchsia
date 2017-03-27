// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <magenta/syscalls.h>

#include <magenta/processargs.h>
#include <launchpad/launchpad.h>

#include "devcoordinator.h"

static mxio_dispatcher_t* coordinator_dispatcher;
static mx_handle_t devhost_job;

static const char* devhost_bin = "/boot/bin/devhost";

mx_status_t coordinator_launch_devhost(const char* name, mx_handle_t hrpc) {
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


mx_status_t coordinator_handler(mx_handle_t h, void* cb, void* cookie) {
    return ERR_NOT_SUPPORTED;
}

static list_node_t driver_list = LIST_INITIAL_VALUE(driver_list);

void coordinator_new_driver(driver_ctx_t* ctx) {
    printf("driver: %s @ %s\n", ctx->drv.name, ctx->libname);
    list_add_tail(&driver_list, &ctx->node);
}

void coordinator_init(mx_handle_t root_job) {
    printf("coordinator_init()\n");

    mx_status_t status = mx_job_create(root_job, 0u, &devhost_job);
    if (status < 0) {
        printf("unable to create devhost job\n");
    }

    mxio_dispatcher_create(&coordinator_dispatcher, coordinator_handler);
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
    mxio_dispatcher_run(coordinator_dispatcher);
}