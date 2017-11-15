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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

#include <zircon/compiler.h>

#include "kpci-private.h"

#include "protocol.c"

// Convenience method for simple replies
static zx_status_t pci_rpc_reply_ok(zx_handle_t ch, pci_msg_t* req) {
    pci_msg_t resp = {
        .txid = req->txid,
        .ordinal = ZX_OK,
    };

    return zx_channel_write(ch, 0, &resp, sizeof(resp), NULL, 0);
};

// kpci is a driver that communicates with the kernel to publish a list of pci devices.
static zx_status_t kpci_get_auxdata(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
    char args[32];
    snprintf(args, sizeof(args), "%s,%02x:%02x:%02x", req->data,
             device->info.bus_id, device->info.dev_id, device->info.func_id);

    uint32_t actual;
    pci_msg_t resp = {
        .txid = req->txid,
    };

    zx_status_t st = pciroot_get_auxdata(&device->pciroot, args, resp.data, req->outlen, &actual);
    if (st != ZX_OK) {
        return st;
    } else {
        resp.ordinal = ZX_OK;
        resp.datalen = actual;
    }

    return zx_channel_write(ch, 0, &resp, sizeof(resp), NULL, 0);
}


static zx_status_t kpci_enable_bus_master(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
    bool enable = req->data[0];

    zx_status_t st = zx_pci_enable_bus_master(device->handle, enable);
    if (st != ZX_OK) {
        return st;
    }

    return pci_rpc_reply_ok(ch, req);
}

// All callbacks corresponding to protocol operations match this signature.
// Rather than passing the outgoing message back to kpci_rxrpc, the callback
// itself is expected to write to the channel directly. This greatly simplifies
// lifecycles around handles that need to be passed to/from the proxy devhost,
// as well as keeping the method  declaration simpler. In the event of an error
// the callback can return the error code back to kpci_rxrpc and it will handle
// sending it back over the channel.
typedef zx_status_t (*rxrpc_cbk_t)(pci_msg_t*, kpci_device_t*, zx_handle_t);
rxrpc_cbk_t rxrpc_cbk_tbl[] = {
        [PCI_OP_RESET_DEVICE] = NULL,
        [PCI_OP_ENABLE_BUS_MASTER] = kpci_enable_bus_master,
        [PCI_OP_ENABLE_PIO] = NULL,
        [PCI_OP_CONFIG_READ] = NULL,
        [PCI_OP_GET_NEXT_CAPABILITY] = NULL,
        [PCI_OP_GET_RESOURCE] = NULL,
        [PCI_OP_QUERY_IRQ_MODE_CAPS] = NULL,
        [PCI_OP_SET_IRQ_MODE] = NULL,
        [PCI_OP_MAP_INTERRUPT] = NULL,
        [PCI_OP_GET_DEVICE_INFO] = NULL,
        [PCI_OP_GET_AUXDATA] = kpci_get_auxdata,
        [PCI_OP_MAX] = NULL,
};

static zx_status_t kpci_rxrpc(void* ctx, zx_handle_t ch) {
#if PROXY_DEVICE
    return ZX_ERR_NOT_SUPPORTED;
#else
    kpci_device_t* device = ctx;
    const char* name = device_get_name(device->zxdev);
    pci_msg_t req;
    uint32_t actual_bytes;
    zx_status_t st = zx_channel_read(ch, 0, &req, NULL, sizeof(req), 0, &actual_bytes, NULL);
    if (st != ZX_OK) {
        return st;
    }

    if (actual_bytes != sizeof(req)) {
        return ZX_ERR_INTERNAL;
    }

    uint32_t op = req.ordinal;
    if (op >= PCI_OP_MAX || rxrpc_cbk_tbl[op] == NULL) {
        st = ZX_ERR_NOT_SUPPORTED;
        goto err;
    }

    zxlogf(SPEW, "pci[%s]: rpc-in op %d args '%#02x %#02x %#02x %#02x...'\n", name, op,
        req.data[0], req.data[1], req.data[2], req.data[3]);
    st = rxrpc_cbk_tbl[req.ordinal](&req, device, ch);
    if (st != ZX_OK) {
        goto err;
    }

    zxlogf(SPEW, "pci[%s]: rpc-in op %d returned success\n", name, op);
    return st;

err : {
    pci_msg_t resp = {
        .txid = req.txid,
        .ordinal = st,
    };

    zxlogf(SPEW, "pci[%s]: rpc-in op %d error %d\n", name, req.ordinal, st);
    return zx_channel_write(ch, 0, &resp, sizeof(resp), NULL, 0);
}
#endif
}

static void kpci_release(void* ctx) {
    kpci_device_t* device = ctx;
    if (device->handle != ZX_HANDLE_INVALID) {
        zx_handle_close(device->handle);
    }
    free(device);
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
        {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
        {BIND_PCI_VID, 0, info.vendor_id},
        {BIND_PCI_DID, 0, info.device_id},
        {BIND_PCI_CLASS, 0, info.base_class},
        {BIND_PCI_SUBCLASS, 0, info.sub_class},
        {BIND_PCI_INTERFACE, 0, info.program_interface},
        {BIND_PCI_REVISION, 0, info.revision_id},
        {BIND_PCI_BDF_ADDR, 0, BIND_PCI_BDF_PACK(info.bus_id, info.dev_id, info.func_id)},
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

// clang-format off
ZIRCON_DRIVER_BEGIN(pci, kpci_driver_ops, "zircon", "0.1", 1)
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(pci)
// clang-format on

#else

static zx_status_t kpci_drv_bind(void* ctx, zx_device_t* parent) {
    for (uint32_t index = 0;; index++) {
        zx_device_t* dev;
        // TODO(cja): Once the protocol is entirely proxied we will need to
        // revisit handle ownership. Top devhost should keep handles, proxy
        // should not.
        if (kpci_init_child(parent, index, true, ZX_HANDLE_INVALID, &dev) != ZX_OK) {
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
// clang-format off
ZIRCON_DRIVER_BEGIN(pci, kpci_driver_ops, "zircon", "0.1", 5)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_PCIROOT),
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_KPCI),
ZIRCON_DRIVER_END(pci)
#endif
