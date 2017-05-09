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

static void kpci_release(void* ctx) {
    kpci_device_t* device = ctx;
    if (device->handle != MX_HANDLE_INVALID) {
        mx_handle_close(device->handle);
    }
    free(device);
}

static mx_protocol_device_t kpci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = kpci_release,
};

// initializes and optionally adds a new child device
// device will be added if parent is not NULL
static mx_status_t kpci_init_child(mx_driver_t* drv, mx_device_t* parent,
                                   uint32_t index, bool save_handle, mx_device_t** out) {
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
    memcpy(&device->info, &info, sizeof(info));
    if (save_handle) {
        device->handle = handle;
    } else {
        mx_handle_close(handle);
        handle = MX_HANDLE_INVALID;
    }
    device->index = index;

    char name[20];
    snprintf(name, sizeof(name), "%02x:%02x:%02x", info.bus_id, info.dev_id, info.func_id);

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

    mx_status_t status;
    if (parent) {
        char busdev_args[32];
        snprintf(busdev_args, sizeof(busdev_args), "%u", index);

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = device,
            .driver = drv,
            .ops = &kpci_device_proto,
            .proto_id = MX_PROTOCOL_PCI,
            .proto_ops = &_pci_protocol,
            .props = device->props,
            .prop_count = countof(device->props),
            .busdev_args = busdev_args,
            .rsrc = MX_HANDLE_INVALID,
            .flags = DEVICE_ADD_BUSDEV,
        };

        status = device_add(parent, &args, &device->mxdev);
    } else {
#if NEW_BUS_DRIVER
        return ERR_BAD_STATE;
#else
        status = device_create(name, device, &kpci_device_proto, drv,
                               MX_PROTOCOL_PCI, &_pci_protocol, &device->mxdev);
        if (status == NO_ERROR) {
            // TODO (voydanoff) devhost_create_pcidev() requires that we write directly into the mx_device_t here.
            // This can be cleaned up after we remove devhost_create_pcidev()
            device->mxdev->props = device->props;
            device->mxdev->prop_count = countof(device->props);
        }
#endif
    }

    if (status == NO_ERROR) {
        *out = device->mxdev;
    } else {
        if (handle != MX_HANDLE_INVALID) {
            mx_handle_close(handle);
        }
        free(device);
    }

    return status;
}

#if NEW_BUS_DRIVER
static mx_status_t kpci_drv_bind(mx_driver_t* drv, mx_device_t* parent, void** cookie) {
    mx_status_t status;
    mx_device_t* pcidev;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "pci",
        .driver = drv,
        .ops =  &kpci_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    if ((status = device_add(parent, &args, &pcidev)) < 0) {
        return status;
    }
    for (uint32_t index = 0;; index++) {
        mx_device_t* dev;
        // don't hang onto the PCI handle - we don't need it any more
        if (kpci_init_child(drv, pcidev, index, false, &dev) != NO_ERROR) {
            break;
        }
    }
    return NO_ERROR;
}

static mx_status_t kpci_drv_create(mx_driver_t* drv, mx_device_t* parent,
                                   const char* name, const char* args, mx_handle_t resource) {
    if (resource != MX_HANDLE_INVALID) {
        mx_handle_close(resource);
    }
    uint32_t index = strtoul(args, NULL, 10);
    mx_device_t* dev;
    return kpci_init_child(drv, parent, index, true, &dev);
}

#else
static mx_driver_t __driver_kpci = {
    .name = "pci",
};

mx_status_t devhost_create_pcidev(mx_device_t** out, uint32_t index) {
    return kpci_init_child(&__driver_kpci, NULL, index, true, out);
}

void devhost_launch_devhost(mx_device_t* parent, const char* name, uint32_t protocol_id,
                            const char* procname, int argc, char** argv);

static mx_status_t kpci_init_children(mx_driver_t* drv, mx_device_t* parent) {
    for (uint32_t index = 0;; index++) {
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
    }

    return NO_ERROR;
}

static mx_status_t kpci_drv_init(mx_driver_t* drv) {
    mx_status_t status;
    printf("kpci_init()\n");

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "pci",
        .driver = drv,
        .ops = &kpci_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    mx_device_t* kpci_root_dev;
    if ((status = device_add(driver_get_root_device(), &args, &kpci_root_dev)) < 0) {
        return status;
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
