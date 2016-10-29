// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/acpi.h>

#include <acpisvc/simple.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acpi.h"
#include "devhost.h"

typedef struct acpi_device {
    mx_device_t device;
    char hid[8];
    acpi_handle_t handle;
} acpi_device_t;

#define get_acpi_device(dev) containerof(dev, acpi_device_t, device)

static mx_handle_t acpi_device_clone_handle(mx_device_t* dev) {
    acpi_device_t* device = get_acpi_device(dev);
    return acpi_clone_handle(&device->handle);
}

static mx_acpi_protocol_t acpi_device_acpi_proto = {
    .clone_handle = acpi_device_clone_handle,
};

static mx_protocol_device_t acpi_device_proto = {
};

static mx_status_t acpi_get_child_handle_by_hid(acpi_handle_t* h, const char* hid, acpi_handle_t* child) {
    char name[4] = {0};
    {
        acpi_rsp_list_children_t* rsp;
        size_t len;
        mx_status_t status = acpi_list_children(h, &rsp, &len);
        if (status != NO_ERROR) {
            return status;
        }

        for (uint32_t i = 0; i < rsp->num_children; ++i) {
            if (!memcmp(rsp->children[i].hid, hid, 7)) {
                memcpy(name, rsp->children[i].name, 4);
                break;
            }
        }
        free(rsp);

        if (name[0] == 0) {
            return ERR_NOT_FOUND;
        }
    }
    return acpi_get_child_handle(h, name, child);
}

void devhost_launch_devhost(mx_device_t* parent, const char* name, uint32_t protocol_id, const char* procname, int argc, char** argv);

mx_status_t acpi_init(mx_driver_t* driver) {
    // launch the acpi devhost
    char arg1[20];
    snprintf(arg1, sizeof(arg1), "acpi");

    const char* args[2] = { "/boot/bin/devhost", arg1};
    devhost_launch_devhost(driver_get_root_device(), "acpi", MX_PROTOCOL_ACPI, "devhost:acpi", 2, (char**)args);
    return NO_ERROR;
}

mx_driver_t _driver_acpi = {
    .ops = {
        .init = acpi_init,
    },
};

extern mx_handle_t devhost_get_hacpi(void);

mx_status_t devhost_init_acpidev(mx_device_t** out) {
    // Find the battery device.
    // TODO(yky,teisenbe) The battery device is in _SB.PCI0 on the acer. To be replaced by real
    // acpi device publishing code.

    mx_handle_t hacpi = devhost_get_hacpi();
    if (hacpi <= 0) {
        printf("no acpi root handle\n");
        return ERR_NOT_SUPPORTED;
    }

    acpi_handle_t acpi_root, pcie_handle;
    acpi_handle_init(&acpi_root, hacpi);

    mx_status_t status = acpi_get_child_handle_by_hid(&acpi_root, "PNP0A08", &pcie_handle);
    if (status != NO_ERROR) {
        printf("no pcie handle\n");
        acpi_handle_close(&acpi_root);
        return ERR_NOT_SUPPORTED;
    }
    acpi_handle_close(&acpi_root);

    acpi_device_t* batt_dev = calloc(1, sizeof(acpi_device_t));
    status = acpi_get_child_handle_by_hid(&pcie_handle, "PNP0C0A", &batt_dev->handle);
    if (status != NO_ERROR) {
        free(batt_dev);
    } else {
        memcpy(batt_dev->hid, "PNP0C0A", 7);
        device_init(&batt_dev->device, &_driver_acpi, "BAT0", &acpi_device_proto);
        batt_dev->device.protocol_id = MX_PROTOCOL_ACPI;
        batt_dev->device.protocol_ops = &acpi_device_acpi_proto;
        // FIXME device is added in devhost.c
    }

    acpi_handle_close(&pcie_handle);
    *out = &batt_dev->device;
    return NO_ERROR;
}
