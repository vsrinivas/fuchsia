// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/platform-devices.h>

#include <hw/pci.h>
#include <zircon/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <zircon/compiler.h>

#include "kpci-private.h"

#include "protocol.c"

// kpci is a driver that communicates with the kernel to publish a list of pci devices.

static void kpci_release(void* ctx) {
    kpci_device_t* device = ctx;
    if (device->handle != ZX_HANDLE_INVALID) {
        zx_handle_close(device->handle);
    }
    free(device);
}

static zx_protocol_device_t kpci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = kpci_release,
};

// initializes and optionally adds a new child device
// device will be added if parent is not NULL
static zx_status_t kpci_init_child(zx_device_t* parent, uint32_t index, bool save_handle,
                                   zx_device_t** out) {
    zx_pcie_device_info_t info;
    zx_handle_t handle;

    zx_status_t status = zx_pci_get_nth_device(get_root_resource(), index, &info, &handle);
    if (status != ZX_OK) {
        return status;
    }

    kpci_device_t* device = calloc(1, sizeof(kpci_device_t));
    if (!device) {
        zx_handle_close(handle);
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(&device->info, &info, sizeof(info));
    if (save_handle) {
        device->handle = handle;
    } else {
        // Work around for ZX-928.  Leak handle here, since closing it would
        // causes bus mastering on the device to be disabled via the dispatcher
        // dtor.  This causes problems for devices that the BIOS owns and a driver
        // needs to execute a protocol with the BIOS in order to claim ownership.
        handle = ZX_HANDLE_INVALID;
    }
    device->index = index;

    char name[20];
    snprintf(name, sizeof(name), "%02x:%02x:%02x", info.bus_id, info.dev_id, info.func_id);

    zx_device_prop_t device_props[] = {
        (zx_device_prop_t){ BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI },
        (zx_device_prop_t){ BIND_PCI_VID, 0, info.vendor_id },
        (zx_device_prop_t){ BIND_PCI_DID, 0, info.device_id },
        (zx_device_prop_t){ BIND_PCI_CLASS, 0, info.base_class },
        (zx_device_prop_t){ BIND_PCI_SUBCLASS, 0, info.sub_class },
        (zx_device_prop_t){ BIND_PCI_INTERFACE, 0, info.program_interface },
        (zx_device_prop_t){ BIND_PCI_REVISION, 0, info.revision_id },
        (zx_device_prop_t){ BIND_PCI_BDF_ADDR, 0, BIND_PCI_BDF_PACK(info.bus_id,
                                                                    info.dev_id,
                                                                    info.func_id) },
    };

    static_assert(sizeof(device_props) == sizeof(device->props),
                 "Invalid number of PCI properties in kpci_device_t!");

    memcpy(device->props, device_props, sizeof(device->props));

    if (parent) {
        char busdev_args[64];
        snprintf(busdev_args, sizeof(busdev_args),
                 "pci#%u:%04x:%04x,%u", index,
                 info.vendor_id, info.device_id, index);

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = device,
            .ops = &kpci_device_proto,
            .proto_id = ZX_PROTOCOL_PCI,
            .proto_ops = &_pci_protocol,
            .props = device->props,
            .prop_count = countof(device->props),
            .busdev_args = busdev_args,
            .rsrc = ZX_HANDLE_INVALID,
            .flags = DEVICE_ADD_BUSDEV,
        };

        status = device_add(parent, &args, &device->mxdev);
    } else {
        return ZX_ERR_BAD_STATE;
    }

    if (status == ZX_OK) {
        *out = device->mxdev;
    } else {
        if (handle != ZX_HANDLE_INVALID) {
            zx_handle_close(handle);
        }
        free(device);
    }

    return status;
}

static zx_status_t kpci_drv_bind(void* ctx, zx_device_t* parent, void** cookie) {
    zx_status_t status;
    zx_device_t* pcidev;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "pci",
        .ops =  &kpci_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    if ((status = device_add(parent, &args, &pcidev)) < 0) {
        return status;
    }
    for (uint32_t index = 0;; index++) {
        zx_device_t* dev;
        // don't hang onto the PCI handle - we don't need it any more
        if (kpci_init_child(pcidev, index, false, &dev) != ZX_OK) {
            break;
        }
    }
    return ZX_OK;
}

static zx_status_t kpci_drv_create(void* ctx, zx_device_t* parent,
                                   const char* name, const char* args, zx_handle_t resource) {
    if (resource != ZX_HANDLE_INVALID) {
        zx_handle_close(resource);
    }
    uint32_t index = strtoul(args, NULL, 10);
    zx_device_t* dev;
    return kpci_init_child(parent, index, true, &dev);
}

static zx_driver_ops_t kpci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = kpci_drv_bind,
    .create = kpci_drv_create,
};

// TODO(voydanoff): mdi driver should publish a device with ZX_PROTOCOL_PCIROOT
ZIRCON_DRIVER_BEGIN(pci, kpci_driver_ops, "zircon", "0.1", 5)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_PCIROOT),
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_KPCI),
ZIRCON_DRIVER_END(pci)
