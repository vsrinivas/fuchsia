// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>

#include <hw/pci.h>
#include <magenta/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <magenta/compiler.h>

#include "kpci-private.h"

#include "protocol.c"

// kpci is a driver that communicates with the kernel to publish a list of pci devices.

static mx_status_t kpci_release(mx_device_t* dev) {
    kpci_device_t* device = get_kpci_device(dev);
    mx_handle_close(device->handle);
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t kpci_device_proto = {
    .release = kpci_release,
};

static mx_status_t kpci_init_child(mx_driver_t* drv, mx_device_t** out, uint32_t index) {
    mx_pcie_get_nth_info_t info;

    mx_handle_t handle = mx_pci_get_nth_device(get_root_resource(), index, &info);
    if (handle < 0) {
        return handle;
    }

    kpci_device_t* device = calloc(1, sizeof(kpci_device_t));
    if (!device) {
        mx_handle_close(handle);
        return ERR_NO_MEMORY;
    }

    char name[20];
    snprintf(name, sizeof(name), "%02x:%02x:%02x", info.bus_id, info.dev_id, info.func_id);
    device_init(&device->device, drv, name, &kpci_device_proto);

    device->device.protocol_id = MX_PROTOCOL_PCI;
    device->device.protocol_ops = &_pci_protocol;
    device->handle = handle;
    device->index = index;
    *out = &device->device;

    device->props[0] = (mx_device_prop_t){ BIND_PROTOCOL, 0, MX_PROTOCOL_PCI };
    device->props[1] = (mx_device_prop_t){ BIND_PCI_VID, 0, info.vendor_id };
    device->props[2] = (mx_device_prop_t){ BIND_PCI_DID, 0, info.device_id };
    device->props[3] = (mx_device_prop_t){ BIND_PCI_CLASS, 0, info.base_class };
    device->props[4] = (mx_device_prop_t){ BIND_PCI_SUBCLASS, 0, info.sub_class };
    device->props[5] = (mx_device_prop_t){ BIND_PCI_INTERFACE, 0, info.program_interface };
    device->props[6] = (mx_device_prop_t){ BIND_PCI_REVISION, 0, info.revision_id };
    device->device.props = device->props;
    device->device.prop_count = countof(device->props);

    memcpy(&device->info, &info, sizeof(info));

    return NO_ERROR;
}

static mx_driver_t __driver_kpci = {
    .name = "pci",
};

mx_status_t devhost_create_pcidev(mx_device_t** out, uint32_t index) {
    return kpci_init_child(&__driver_kpci, out, index);
}

static mx_device_t* kpci_root_dev;

void devhost_launch_devhost(mx_device_t* parent, const char* name, uint32_t protocol_id,
                            const char* procname, int argc, char** argv);

static mx_status_t kpci_init_children(mx_driver_t* drv, mx_device_t* parent) {
    for (uint32_t index = 0;; index++) {
#if ONLY_ONE_DEVHOST
        mx_device_t* device;
        if (kpci_init_child(drv, &device, index) != NO_ERROR) {
            break;
        }
        device_add(device, parent);
#else
        mx_pcie_get_nth_info_t info;
        mx_handle_t h = mx_pci_get_nth_device(get_root_resource(), index, &info);
        if (h < 0) {
            break;
        }
        mx_handle_close(h);

        char name[32];
        snprintf(name, sizeof(name), "%02x:%02x:%02x",
                 info.bus_id, info.dev_id, info.func_id);

        char procname[64];
        snprintf(procname, sizeof(procname), "devhost:pci#%d:%04x:%04x",
                 index, info.vendor_id, info.device_id);

        char arg1[20];
        snprintf(arg1, sizeof(arg1), "pci=%d", index);

        const char* args[2] = { "/boot/bin/devhost", arg1 };
        devhost_launch_devhost(parent, name, MX_PROTOCOL_PCI, procname, 2, (char**)args);
#endif
    }

    return NO_ERROR;
}

static mx_status_t kpci_drv_init(mx_driver_t* drv) {
    mx_status_t status;
    printf("kpci_init()\n");

    if ((status = device_create(&kpci_root_dev, drv, "pci", &kpci_device_proto))) {
        return status;
    }

    // make the pci root non-bindable
    device_set_bindable(kpci_root_dev, false);

    if (device_add(kpci_root_dev, driver_get_root_device()) < 0) {
        free(kpci_root_dev);
        return NO_ERROR;
    } else {
        return kpci_init_children(drv, kpci_root_dev);
    }
}

mx_driver_t _driver_kpci = {
    .ops = {
        .init = kpci_drv_init,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_kpci, "pci", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_kpci)
