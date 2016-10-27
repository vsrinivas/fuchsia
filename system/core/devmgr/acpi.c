// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>

#include <acpisvc/simple.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>

#include "devmgr.h"
#include "devhost.h"

static acpi_handle_t acpi_root;

mx_status_t devmgr_launch_acpisvc(void) {
    const char* binname = "/boot/bin/acpisvc";

    const char* args[3] = {
        binname,
    };

    mx_handle_t acpi_comm[2] = {0};

    mx_status_t status = mx_msgpipe_create(acpi_comm, 0);
    if (status != NO_ERROR) {
        goto cleanup_handles;
    }

    mx_handle_t logger = mx_log_create(0);
    if (logger < 0) {
        status = logger;
        goto cleanup_handles;
    }

    mx_handle_t hnd[3];
    uint32_t ids[3];
    ids[0] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, MXIO_FLAG_USE_FOR_STDIO | 1);
    hnd[0] = logger;
    ids[1] = MX_HND_TYPE_USER0;
    hnd[1] = mx_handle_duplicate(get_root_resource(), MX_RIGHT_SAME_RIGHTS);
    ids[2] = MX_HND_TYPE_USER1;
    hnd[2] = acpi_comm[1];

    printf("devmgr: launch acpisvc\n");
    mx_handle_t proc = launchpad_launch("acpisvc", 1, args, NULL, 3, hnd, ids);
    if (proc < 0) {
        printf("devmgr: acpisvc launch failed: %d\n", proc);
        status = proc;
        goto cleanup_handles;
    }

    mx_handle_close(proc);

    if (status != NO_ERROR) {
        mx_handle_close(acpi_comm[0]);
        return status;
    }

    acpi_handle_init(&acpi_root, acpi_comm[0]);
    return NO_ERROR;

cleanup_handles:
    if (acpi_comm[0]) {
        mx_handle_close(acpi_comm[0]);
        mx_handle_close(acpi_comm[1]);
    }
    return status;
}

// TODO(teisenbe): Instead of doing this as a single function, give the kpci
// driver a handle to the PCIe root complex ACPI node and let it ask for
// the initialization info.
mx_status_t devmgr_init_pcie(void) {
    char name[4] = {0};
    {
        acpi_rsp_list_children_t* rsp;
        size_t len;
        mx_status_t status = acpi_list_children(&acpi_root, &rsp, &len);
        if (status != NO_ERROR) {
            return status;
        }

        for (uint32_t i = 0; i < rsp->num_children; ++i) {
            if (!memcmp(rsp->children[i].hid, "PNP0A08", 7)) {
                memcpy(name, rsp->children[i].name, 4);
                break;
            }
        }
        free(rsp);

        if (name[0] == 0) {
            return ERR_NOT_FOUND;
        }
    }

    acpi_handle_t pcie_handle;
    mx_status_t status = acpi_get_child_handle(&acpi_root, name, &pcie_handle);
    if (status != NO_ERROR) {
        return status;
    }

    acpi_rsp_get_pci_init_arg_t* rsp;
    size_t len;
    status = acpi_get_pci_init_arg(&pcie_handle, &rsp, &len);
    if (status != NO_ERROR) {
        acpi_handle_close(&pcie_handle);
        return status;
    }
    acpi_handle_close(&pcie_handle);

    len -= offsetof(acpi_rsp_get_pci_init_arg_t, arg);
    status = mx_pci_init(get_root_resource(), &rsp->arg, len);

    free(rsp);
    return status;
}

void devmgr_poweroff(void) {
    acpi_s_state_transition(&acpi_root, ACPI_S_STATE_S5);
    mx_debug_send_command(get_root_resource(), "poweroff", sizeof("poweroff"));
}

void devmgr_reboot(void) {
    acpi_s_state_transition(&acpi_root, ACPI_S_STATE_REBOOT);
    mx_debug_send_command(get_root_resource(), "reboot", sizeof("reboot"));
}

void devmgr_acpi_ps0(char* arg) {
    acpi_ps0(&acpi_root, arg, strlen(arg));
}

mx_handle_t devmgr_acpi_clone(void) {
    return acpi_clone_handle(&acpi_root);
}
