// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/platform-defs.h>

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

static zx_status_t kpci_rxrpc(void* ctx, zx_handle_t h) {
#if PROXY_DEVICE
    return ZX_ERR_NOT_SUPPORTED;
#else
    pci_msg_t req;
    zx_status_t st = zx_channel_read(h, 0, &req, NULL, sizeof(req), 0, NULL, NULL);
    if (st != ZX_OK) {
        return st;
    }

    pci_msg_t resp = {
        .txid = req.txid,
        .reserved0 = 0,
        .flags = 0,
        .datalen = 0,
    };
    if (req.ordinal != PCI_OP_GET_AUXDATA) {
        resp.ordinal = ZX_ERR_NOT_SUPPORTED;
        goto out;
    }

    zxlogf(SPEW, "pci: rpc-in op %d args '%s'\n", req.ordinal, req.data);

    kpci_device_t* device = ctx;
    char args[32];
    snprintf(args, sizeof(args), "%s,%02x:%02x:%02x", req.data,
            device->info.bus_id, device->info.dev_id, device->info.func_id);

    uint32_t actual;
    st = pciroot_get_auxdata(&device->pciroot, args, resp.data, req.outlen, &actual);
    if (st != ZX_OK) {
        resp.ordinal = st;
    } else {
        resp.ordinal = ZX_OK;
        resp.datalen = actual;
    }

out:
    return zx_channel_write(h, 0, &resp, sizeof(resp), NULL, 0);
#endif
}

static zx_protocol_device_t kpci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .rxrpc = kpci_rxrpc,
    .release = kpci_release,
};

// initializes and optionally adds a new child device
// device will be added if parent is not NULL
static zx_status_t kpci_init_child(zx_device_t* parent, uint32_t index,
                                   bool save_handle, zx_handle_t rpcch,
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

#if PROXY_DEVICE
    device->pciroot_rpcch = rpcch;
#else
    status = device_get_protocol(parent, ZX_PROTOCOL_PCIROOT, &device->pciroot);
#endif

    char name[20];
    snprintf(name, sizeof(name), "%02x:%02x:%02x", info.bus_id, info.dev_id, info.func_id);

#if !PROXY_DEVICE
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
#endif

    if (parent) {
        char argstr[64];
        snprintf(argstr, sizeof(argstr),
                 "pci#%u:%04x:%04x,%u", index,
                 info.vendor_id, info.device_id, index);

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = device,
            .ops = &kpci_device_proto,
            .proto_id = ZX_PROTOCOL_PCI,
            .proto_ops = &_pci_protocol,
#if !PROXY_DEVICE
            .props = device_props,
            .prop_count = countof(device_props),
            .proxy_args = argstr,
            .flags = DEVICE_ADD_MUST_ISOLATE,
#endif
        };

        status = device_add(parent, &args, &device->zxdev);
    } else {
        return ZX_ERR_BAD_STATE;
    }

    if (status == ZX_OK) {
        *out = device->zxdev;
    } else {
        if (handle != ZX_HANDLE_INVALID) {
            zx_handle_close(handle);
        }
        free(device);
    }

    return status;
}

#if PROXY_DEVICE

static zx_status_t kpci_drv_create(void* ctx, zx_device_t* parent,
                                   const char* name, const char* args,
                                   zx_handle_t rpcch) {
    uint32_t index = strtoul(args, NULL, 10);
    zx_device_t* dev;
    return kpci_init_child(parent, index, true, rpcch, &dev);
}

static zx_driver_ops_t kpci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .create = kpci_drv_create,
};

ZIRCON_DRIVER_BEGIN(pci, kpci_driver_ops, "zircon", "0.1", 1)
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(pci)

#else

static zx_status_t kpci_drv_bind(void* ctx, zx_device_t* parent) {
    for (uint32_t index = 0;; index++) {
        zx_device_t* dev;
        // don't hang onto the PCI handle - we don't need it any more
        if (kpci_init_child(parent, index, false, ZX_HANDLE_INVALID, &dev) != ZX_OK) {
            break;
        }
    }
    return ZX_OK;
}

static zx_driver_ops_t kpci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = kpci_drv_bind,
};

// TODO(voydanoff): mdi driver should publish a device with ZX_PROTOCOL_PCIROOT
ZIRCON_DRIVER_BEGIN(pci, kpci_driver_ops, "zircon", "0.1", 5)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_PCIROOT),
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_KPCI),
ZIRCON_DRIVER_END(pci)
#endif
