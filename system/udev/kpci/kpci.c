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
    kpci_device_t* device = dev->ctx;
    mx_handle_close(device->handle);
    device_destroy(device->mxdev);
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
    mx_status_t status = device_create(name, device, &kpci_device_proto, drv, &device->mxdev);
    if (status != NO_ERROR) {
        free(device);
        return status;
    }

    device_set_protocol(device->mxdev, MX_PROTOCOL_PCI, &_pci_protocol);
    device->handle = handle;
    device->index = index;
    *out = device->mxdev;

    mx_device_prop_t device_props[] = {
        (mx_device_prop_t){ BIND_PROTOCOL, 0, MX_PROTOCOL_PCI },
        (mx_device_prop_t){ BIND_PCI_VID, 0, info.vendor_id },
        (mx_device_prop_t){ BIND_PCI_DID, 0, info.device_id },
        (mx_device_prop_t){ BIND_PCI_CLASS, 0, info.base_class },
        (mx_device_prop_t){ BIND_PCI_SUBCLASS, 0, info.sub_class },
        (mx_device_prop_t){ BIND_PCI_INTERFACE, 0, info.program_interface },
        (mx_device_prop_t){ BIND_PCI_REVISION, 0, info.revision_id },
        (mx_device_prop_t){ BIND_PCI_BDF_ADDR, 0, BIND_PCI_BDF_PACK(info.bus_id,
                                                                    info.dev_id,
                                                                    info.func_id) },
    };

    static_assert(sizeof(device_props) == sizeof(device->props),
                 "Invalid number of PCI properties in kpci_device_t!");

    memcpy(device->props, device_props, sizeof(device->props));
    // TODO - devhost_create_pcidev() requires that we write directly into the mx_device_t here.
    // This can be cleaned up after we untangle this driver from the devhost internals
    device->mxdev->props = device->props;
    device->mxdev->prop_count = countof(device->props);

    memcpy(&device->info, &info, sizeof(info));

    return NO_ERROR;
}

#if NEW_BUS_DRIVER
static mx_status_t kpci_drv_bind(mx_driver_t* drv, mx_device_t* parent, void** cookie) {
    mx_status_t status;
    mx_device_t* pcidev;
    if ((status = device_create("pci", NULL, &kpci_device_proto, drv, &pcidev)) < 0) {
        return status;
    }
    if ((status = device_add(pcidev, parent)) < 0) {
        device_destroy(pcidev);
        return status;
    }
    for (uint32_t index = 0;; index++) {
        mx_device_t* dev;
        if (kpci_init_child(drv, &dev, index) != NO_ERROR) {
            break;
        }
        char args[32];
        snprintf(args, sizeof(args), "%u", index);
        device_add_busdev(dev, pcidev, dev->props, dev->prop_count, args,
                          MX_HANDLE_INVALID);
    }
    return NO_ERROR;
}

static mx_status_t kpci_drv_create(mx_driver_t* drv, const char* name,
                                   const char* args, mx_handle_t resource,
                                   mx_device_t** out) {
    if (resource != MX_HANDLE_INVALID) {
        mx_handle_close(resource);
    }
    uint32_t index = strtoul(args, NULL, 10);
    return kpci_init_child(drv, out, index);
}

#else
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
        device_add_with_props(device, parent, device->props, device->prop_count);
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

    if ((status = device_create("pci", NULL, &kpci_device_proto, drv, &kpci_root_dev))) {
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
#endif

static mx_driver_ops_t kpci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
#if NEW_BUS_DRIVER
    .bind = kpci_drv_bind,
    .create = kpci_drv_create,
#else
    .init = kpci_drv_init,
#endif
};

MAGENTA_DRIVER_BEGIN(pci, kpci_driver_ops, "magenta", "0.1", 0)
MAGENTA_DRIVER_END(pci)
