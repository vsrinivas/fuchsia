// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/pci.h>

#include <assert.h>
#include <limits.h>
#include <zircon/assert.h>
#include <zircon/process.h>

#include "kpci-private.h"

// kpci_op_* methods are called by the proxy devhost. For each PCI
// protocol method there is generally a kpci_op_* method for the proxy
// devhost and a corresponding kpci_* method in the top devhost that the
// protocol request is handled by.
_Atomic zx_txid_t pci_global_txid = 0;

static zx_status_t kpci_op_enable_bus_master(void* ctx, bool enable) {
    kpci_device_t* device = ctx;
    return zx_pci_enable_bus_master(device->handle, enable);
}

static zx_status_t kpci_op_enable_pio(void* ctx, bool enable) {
    kpci_device_t* device = ctx;
    return zx_pci_enable_pio(device->handle, enable);
}

static zx_status_t kpci_op_reset_device(void* ctx) {
    kpci_device_t* device = ctx;
    return zx_pci_reset_device(device->handle);
}

// TODO(cja): Figure out how to handle passing PIO privileges to other
// processes in the future when PCI is moved out of the kernel into
// userspace.
static zx_status_t do_resource_bookkeeping(zx_pci_resource_t* res) {
    zx_status_t status;

    if (!res) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch(res->type) {
    case PCI_RESOURCE_TYPE_PIO:
#if __x86_64__
            // x86 PIO space access requires permission in the I/O bitmap
        status = zx_mmap_device_io(get_root_resource(), res->pio_addr, res->size);
#else
        status = ZX_ERR_NOT_SUPPORTED;
#endif
        break;
    default:
        status = ZX_OK;
    }

    return status;
}


// These reads are proxied directly over to the device's PciConfig object so the validity of the
// widths and offsets will be validated on that end and then trickle back to this level of the
// protocol.
//
// In the case of config and capability reads/writes, failure is a catastrophic occurrence
// along the lines of hardware failure or a device being removed from the bus. Due to this,
// those statuses will be asserted upon rather than forcing callers to add additional checks
// every time they wish to do a config read / write.
static uint32_t kpci_op_config_read(void* ctx, uint8_t offset, size_t width) {
    ZX_DEBUG_ASSERT(ctx);
    kpci_device_t* device = ctx;
    uint32_t val;

    // TODO(cja): Investigate whether config reads / writes should return status codes
    // so that failures (largely around bad offsets) can be signaled.
    zx_status_t status = zx_pci_config_read(device->handle, offset & 0xFFF, width, &val);
    ZX_ASSERT_MSG(status == ZX_OK, "pci_config_read: %d\n", status);
    return val;
}

static uint8_t kpci_op_get_next_capability(void* ctx, uint8_t offset, uint8_t type) {
    uint8_t cap_offset = (uint8_t)kpci_op_config_read(ctx, offset + 1, 8);
    uint8_t limit = 64;

    // Walk the capability list looking for the type requested, starting at the offset
    // passed in. limit acts as a barrier in case of an invalid capability pointer list
    // that causes us to iterate forever otherwise.
    while (cap_offset != 0 && limit--) {
        uint8_t type_id = (uint8_t)kpci_op_config_read(ctx, cap_offset, 8);
        if (type_id == type) {
            return cap_offset;
        }

        // We didn't find the right type, move on
        cap_offset = (uint8_t)kpci_op_config_read(ctx, cap_offset + 1, 8);
    }

    // No more entries are in the list
    return 0;
}

static zx_status_t kpci_op_get_resource(void* ctx, uint32_t res_id, zx_pci_resource_t* out_res) {
    zx_status_t status = ZX_OK;

    if (!out_res || res_id >= PCI_RESOURCE_COUNT) {
        return ZX_ERR_INVALID_ARGS;
    }

    kpci_device_t* device = ctx;

    switch (res_id) {
        case PCI_RESOURCE_BAR_0:
        case PCI_RESOURCE_BAR_1:
        case PCI_RESOURCE_BAR_2:
        case PCI_RESOURCE_BAR_3:
        case PCI_RESOURCE_BAR_4:
        case PCI_RESOURCE_BAR_5:
            status = zx_pci_get_bar(device->handle, res_id, out_res);
            break;
        case PCI_RESOURCE_CONFIG:
            status = zx_pci_get_config(device->handle, out_res);
            break;
    }

    if (status != ZX_OK) {
        return status;
    }

    return do_resource_bookkeeping(out_res);
}

// Sanity check the resource enum
static_assert(PCI_RESOURCE_BAR_0 == 0, "BAR 0's value is not 0");
static_assert(PCI_RESOURCE_BAR_5 == 5, "BAR 5's value is not 5");
static_assert(PCI_RESOURCE_CONFIG > PCI_RESOURCE_BAR_5, "resource order in the enum is wrong");

/* Get a resource from the pci bus driver and map for the driver. */
static zx_status_t kpci_op_map_resource(void* ctx,
                                    uint32_t res_id,
                                    zx_cache_policy_t cache_policy,
                                    void** vaddr,
                                    size_t* size,
                                    zx_handle_t* out_handle) {
    if (!vaddr || !size || !out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_pci_resource_t resource;
    zx_status_t status = kpci_op_get_resource(ctx, res_id, &resource);
    if (status != ZX_OK) {
        return status;
    }

    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (resource.type == PCI_RESOURCE_TYPE_PIO) {
        return ZX_ERR_WRONG_TYPE;
    }

    uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_MAP_RANGE;
    if (res_id <= PCI_RESOURCE_BAR_5) {
        // Writes to bar resources are allowed.
        map_flags |= ZX_VM_FLAG_PERM_WRITE;

        // Bar cache policy can be controlled by the driver.
        status = zx_vmo_set_cache_policy(resource.mmio_handle, cache_policy);
        if (status != ZX_OK) {
            zx_handle_close(resource.mmio_handle);
            return status;
        }
    }

    // Map the config/bar passed in. Mappings require PAGE_SIZE alignment for
    // both base and size
    void* vaddr_tmp;
    status = zx_vmar_map(zx_vmar_root_self(), 0, resource.mmio_handle, 0,
                            ROUNDUP(resource.size, PAGE_SIZE),
                            map_flags, (uintptr_t*)&vaddr_tmp);

    if (status != ZX_OK) {
        zx_handle_close(resource.mmio_handle);
        return status;
    }

    *size = resource.size;
    *out_handle = resource.mmio_handle;
    *vaddr = vaddr_tmp;

    return status;
}

static zx_status_t kpci_op_map_interrupt(void* ctx, int which_irq, zx_handle_t* out_handle) {
    zx_status_t status = ZX_OK;

    if (!out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }

    kpci_device_t* device = ctx;
    if (device->handle == ZX_HANDLE_INVALID) {
        return ZX_ERR_BAD_HANDLE;
    }

    status = zx_pci_map_interrupt(device->handle, which_irq, out_handle);
    if (status != ZX_OK) {
        *out_handle = ZX_HANDLE_INVALID;
        return status;
    }

    return ZX_OK;
}

static zx_status_t kpci_op_query_irq_mode_caps(void* ctx,
                                           zx_pci_irq_mode_t mode,
                                           uint32_t* out_max_irqs) {
    kpci_device_t* device = ctx;
    return zx_pci_query_irq_mode_caps(device->handle, mode, out_max_irqs);
}

static zx_status_t kpci_op_set_irq_mode(void* ctx, zx_pci_irq_mode_t mode,
                                    uint32_t requested_irq_count) {
    kpci_device_t* device = ctx;
    return zx_pci_set_irq_mode(device->handle, mode, requested_irq_count);
}

static zx_status_t kpci_op_get_device_info(void* ctx, zx_pcie_device_info_t* out_info) {
    if (out_info == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    kpci_device_t* device = ctx;
    *out_info = device->info;
    return ZX_OK;
}

static zx_status_t kpci_op_get_auxdata(void* ctx, const char* args, void* data,
                                    uint32_t bytes, uint32_t* actual) {
#if PROXY_DEVICE
    kpci_device_t* dev = ctx;
    if (dev->pciroot_rpcch == ZX_HANDLE_INVALID) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    size_t arglen = strlen(args);
    if (arglen > PCI_MAX_DATA) {
        return ZX_ERR_INVALID_ARGS;
    }

    pci_msg_t resp;
    pci_msg_t req = {
        .txid = atomic_fetch_add(&pci_global_txid, 1),
        .reserved0 = 0,
        .flags = 0,
        .ordinal = PCI_OP_GET_AUXDATA,
        .outlen = bytes,
        .datalen = arglen,
    };
    memcpy(req.data, args, arglen);

    zxlogf(SPEW, "pci[%s]: rpc-out op %d args '%s'\n", device_get_name(dev->zxdev), req.ordinal,
           req.data);
    zx_channel_call_args_t cc_args = {
        .wr_bytes = &req,
        .rd_bytes = &resp,
        .wr_num_bytes = sizeof(req),
        .rd_num_bytes = sizeof(resp),
        .wr_handles = NULL,
        .rd_handles = NULL,
        .wr_num_handles = 0,
        .rd_num_handles = 0,
    };
    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t st = zx_channel_call(dev->pciroot_rpcch, 0, ZX_TIME_INFINITE,
                                     &cc_args, &actual_bytes,
                                     &actual_handles, NULL);
    if (st != ZX_OK) {
        return st;
    }
    if (actual_bytes != sizeof(resp)) {
        return ZX_ERR_INTERNAL;
    }
    if (resp.ordinal != ZX_OK) {
        return resp.ordinal;
    }
    if (resp.datalen > bytes) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(data, resp.data, resp.datalen);
    if (actual) {
        *actual = resp.datalen;
    }
    return ZX_OK;
#else
    return ZX_ERR_NOT_SUPPORTED;
#endif
}

static pci_protocol_ops_t _pci_protocol = {
    .enable_bus_master = kpci_op_enable_bus_master,
    .enable_pio = kpci_op_enable_pio,
    .reset_device = kpci_op_reset_device,
    .get_resource = kpci_op_get_resource,
    .map_resource = kpci_op_map_resource,
    .map_interrupt = kpci_op_map_interrupt,
    .query_irq_mode_caps = kpci_op_query_irq_mode_caps,
    .set_irq_mode = kpci_op_set_irq_mode,
    .get_device_info = kpci_op_get_device_info,
    .config_read = kpci_op_config_read,
    .get_next_capability = kpci_op_get_next_capability,
    .get_auxdata = kpci_op_get_auxdata,
};
