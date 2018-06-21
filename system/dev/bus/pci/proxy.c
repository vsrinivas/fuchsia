// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>
#include <string.h>

#include <assert.h>
#include <limits.h>
#include <zircon/assert.h>
#include <zircon/driver/binding.h>
#include <zircon/process.h>

#include "kpci-private.h"

zx_status_t pci_rpc_request(kpci_device_t* dev, uint32_t op, zx_handle_t* handle,
                            pci_msg_t* req, pci_msg_t* resp) {
    if (dev->pciroot_rpcch == ZX_HANDLE_INVALID) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint32_t handle_cnt = 0;
    if (handle) {
        // Since only the caller knows if they expected a valid handle back, make
        // sure the handle reads INVALID if we didn't get one.
        *handle = ZX_HANDLE_INVALID;
        handle_cnt = 1;
    }

    req->ordinal = op;
    zx_channel_call_args_t cc_args = {
        .wr_bytes = req,
        .rd_bytes = resp,
        .rd_handles = handle,
        .wr_num_bytes = sizeof(*req),
        .rd_num_bytes = sizeof(*resp),
        .rd_num_handles = handle_cnt,
    };

    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t st = zx_channel_call(dev->pciroot_rpcch, 0, ZX_TIME_INFINITE,
                                     &cc_args, &actual_bytes, &actual_handles);
    if (st != ZX_OK) {
        return st;
    }

    if (actual_bytes != sizeof(*resp)) {
        return ZX_ERR_INTERNAL;
    }

    return resp->ordinal;
}

// pci_op_* methods are called by the proxy devhost. For each PCI
// protocol method there is generally a pci_op_* method for the proxy
// devhost and a corresponding kpci_* method in the top devhost that the
// protocol request is handled by.

// Enables or disables bus mastering for a particular device.
static zx_status_t pci_op_enable_bus_master(void* ctx, bool enable) {
    kpci_device_t* dev = ctx;
    pci_msg_t req = {
        .enable = enable,
    };
    pci_msg_t resp = {};

    return pci_rpc_request(dev, PCI_OP_ENABLE_BUS_MASTER, NULL, &req, &resp);
}

// Resets the device.
static zx_status_t pci_op_reset_device(void* ctx) {
    kpci_device_t* dev = ctx;
    pci_msg_t req = {};
    pci_msg_t resp = {};

    return pci_rpc_request(dev, PCI_OP_RESET_DEVICE, NULL, &req, &resp);
}

// These reads are proxied directly over to the device's PciConfig object so the validity of the
// widths and offsets will be validated on that end and then trickle back to this level of the
// protocol.
static zx_status_t pci_op_config_read(void* ctx, uint16_t offset, size_t width, uint32_t* val) {
    kpci_device_t* dev = ctx;
    if (width > sizeof(uint32_t) ||
        val == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    pci_msg_t req = {
        .cfg = {
            .offset = offset,
            .width = width,
        },
    };
    pci_msg_t resp = {};
    zx_status_t st = pci_rpc_request(dev, PCI_OP_CONFIG_READ, NULL, &req, &resp);
    if (st == ZX_OK) {
        *val = resp.cfg.value;
    }
    return st;
}

// These reads are proxied directly over to the device's PciConfig object so the validity of the
// widths and offsets will be validated on that end and then trickle back to this level of the
// protocol.
static zx_status_t pci_op_config_write(void* ctx, uint16_t offset, size_t width, uint32_t val) {
    kpci_device_t* dev = ctx;
    if (width > sizeof(uint32_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    pci_msg_t req = {
        .cfg = {
            .offset = offset,
            .width = width,
            .value = val,
        },
    };
    pci_msg_t resp = {};
    return pci_rpc_request(dev, PCI_OP_CONFIG_WRITE, NULL, &req, &resp);
}

static uint8_t pci_op_get_next_capability(void* ctx, uint8_t offset, uint8_t type) {
    uint32_t cap_offset = 0;
    pci_op_config_read(ctx, offset + 1, sizeof(uint8_t), &cap_offset);
    uint8_t limit = 64;
    zx_status_t st;

    // Walk the capability list looking for the type requested, starting at the offset
    // passed in. limit acts as a barrier in case of an invalid capability pointer list
    // that causes us to iterate forever otherwise.
    while (cap_offset != 0 && limit--) {
        uint32_t type_id = 0;
        if ((st = pci_op_config_read(ctx, cap_offset, sizeof(uint8_t), &type_id)) != ZX_OK) {
            zxlogf(ERROR, "%s: error reading type from cap offset %#x: %d\n",
                   __func__, cap_offset, st);
            return 0;
        }

        if (type_id == type) {
            return cap_offset;
        }

        // We didn't find the right type, move on, but ensure we're still
        // within the first 256 bytes of standard config space.
        if (cap_offset >= UINT8_MAX) {
            zxlogf(ERROR, "%s: %#x is an invalid capability offset!\n", __func__, cap_offset);
            return 0;
        }
        if ((st = pci_op_config_read(ctx, cap_offset + 1, sizeof(uint8_t), &cap_offset)) != ZX_OK) {
            zxlogf(ERROR, "%s: error reading next cap from cap offset %#x: %d\n",
                   __func__, cap_offset + 1, st);
            return 0;
        }
    }

    // No more entries are in the list
    return 0;
}

static zx_status_t pci_op_get_bar(void* ctx, uint32_t bar_id, zx_pci_bar_t* out_bar) {
    kpci_device_t* dev = ctx;
    pci_msg_t req = {
        .bar.id = bar_id,
    };
    pci_msg_t resp = {};
    zx_handle_t handle;
    zx_status_t st = pci_rpc_request(dev, PCI_OP_GET_BAR, &handle, &req, &resp);

    if (st == ZX_OK) {
        // Grab the payload and copy the handle over if one was passed back to us
        *out_bar = resp.bar;
        if (out_bar->type == PCI_BAR_TYPE_PIO) {
#if __x86_64__
            // x86 PIO space access requires permission in the I/O bitmap
            // TODO: this is the last remaining use of get_root_resource in pci
            st = zx_mmap_device_io(get_root_resource(), out_bar->addr, out_bar->size);
            if (st != ZX_OK) {
                zxlogf(ERROR, "Failed to map IO window for bar into process: %d\n", st);
                return st;
            }
#else
            zxlogf(INFO, "%s: PIO bars may not be supported correctly on this arch. "
                         "Please have someone check this!\n",
                   __func__);
#endif
        } else {
            out_bar->handle = handle;
        }
    }
    return st;
}

// Map a pci device's bar into the process's address space
static zx_status_t pci_op_map_bar(void* ctx,
                                  uint32_t bar_id,
                                  uint32_t cache_policy,
                                  void** vaddr,
                                  size_t* size,
                                  zx_handle_t* out_handle) {
    if (!vaddr || !size || !out_handle || bar_id >= PCI_MAX_BAR_COUNT) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_pci_bar_t bar;
    zx_status_t status = pci_op_get_bar(ctx, bar_id, &bar);
    if (status != ZX_OK) {
        return status;
    }

    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (bar.type == PCI_BAR_TYPE_PIO || bar.handle == ZX_HANDLE_INVALID) {
        return ZX_ERR_WRONG_TYPE;
    }

    status = zx_vmo_set_cache_policy(bar.handle, cache_policy);
    if (status != ZX_OK) {
        zx_handle_close(bar.handle);
        return status;
    }

    // Map the config/bar passed in. Mappings require PAGE_SIZE alignment for
    // both base and size
    void* vaddr_tmp;
    uint32_t map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE;
    status = zx_vmar_map(zx_vmar_root_self(), 0, bar.handle, 0,
                         ROUNDUP(bar.size, PAGE_SIZE),
                         map_flags, (uintptr_t*)&vaddr_tmp);
    if (status != ZX_OK) {
        zx_handle_close(bar.handle);
        return status;
    }

    *size = bar.size;
    *out_handle = bar.handle;
    *vaddr = vaddr_tmp;

    return status;
}

static zx_status_t pci_op_map_interrupt(void* ctx, int which_irq, zx_handle_t* out_handle) {
    if (!out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }

    kpci_device_t* dev = ctx;
    pci_msg_t req = {
        .irq.which_irq = which_irq,
    };
    pci_msg_t resp = {};
    zx_handle_t handle;
    zx_status_t st = pci_rpc_request(dev, PCI_OP_MAP_INTERRUPT, &handle, &req, &resp);
    if (st == ZX_OK) {
        *out_handle = handle;
    }
    return st;
}

static zx_status_t pci_op_get_bti(void* ctx, uint32_t index, zx_handle_t* out_handle) {
    if (!out_handle) {
        return ZX_ERR_INVALID_ARGS;
    }

    kpci_device_t* dev = ctx;
    pci_msg_t req = { .bti_index = index };
    pci_msg_t resp = {};
    zx_handle_t handle;
    zx_status_t st = pci_rpc_request(dev, PCI_OP_GET_BTI, &handle, &req, &resp);
    if (st == ZX_OK) {
        *out_handle = handle;
    }
    return st;
}

static zx_status_t pci_op_query_irq_mode(void* ctx,
                                         zx_pci_irq_mode_t mode,
                                         uint32_t* out_max_irqs) {
    kpci_device_t* dev = ctx;
    pci_msg_t req = {
        .irq.mode = mode,
    };
    pci_msg_t resp = {};
    zx_status_t st = pci_rpc_request(dev, PCI_OP_QUERY_IRQ_MODE, NULL, &req, &resp);
    if (st == ZX_OK) {
        *out_max_irqs = resp.irq.max_irqs;
    }
    return st;
}

static zx_status_t pci_op_set_irq_mode(void* ctx, zx_pci_irq_mode_t mode,
                                       uint32_t requested_irq_count) {
    kpci_device_t* dev = ctx;
    pci_msg_t req = {
        .irq = {
            .mode = mode,
            .requested_irqs = requested_irq_count,
        },
    };
    pci_msg_t resp = {};
    return pci_rpc_request(dev, PCI_OP_SET_IRQ_MODE, NULL, &req, &resp);
}

static zx_status_t pci_op_get_device_info(void* ctx, zx_pcie_device_info_t* out_info) {
    kpci_device_t* dev = ctx;
    pci_msg_t req = {};
    pci_msg_t resp = {};
    zx_status_t st = pci_rpc_request(dev, PCI_OP_GET_DEVICE_INFO, NULL, &req, &resp);
    if (st == ZX_OK) {
        *out_info = resp.info;
    }
    return st;
}

static zx_status_t pci_op_get_auxdata(void* ctx, const char* args, void* data,
                                      uint32_t bytes, uint32_t* actual) {
    kpci_device_t* dev = ctx;
    size_t arglen = strlen(args);
    if (arglen > PCI_MAX_DATA) {
        return ZX_ERR_INVALID_ARGS;
    }

    pci_msg_t req = {
        .outlen = bytes,
        .datalen = arglen,
    };
    pci_msg_t resp = {};
    memcpy(req.data, args, arglen);
    zx_status_t st = pci_rpc_request(dev, PCI_OP_GET_AUXDATA, NULL, &req, &resp);
    if (st == ZX_OK) {
        if (resp.datalen > bytes) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(data, resp.data, resp.datalen);
        if (actual) {
            *actual = resp.datalen;
        }
    }
    return st;
}

static pci_protocol_ops_t _pci_protocol = {
    .enable_bus_master = pci_op_enable_bus_master,
    .reset_device = pci_op_reset_device,
    .get_bar = pci_op_get_bar,
    .map_bar = pci_op_map_bar,
    .map_interrupt = pci_op_map_interrupt,
    .query_irq_mode = pci_op_query_irq_mode,
    .set_irq_mode = pci_op_set_irq_mode,
    .get_device_info = pci_op_get_device_info,
    .config_read = pci_op_config_read,
    .config_write = pci_op_config_write,
    .get_next_capability = pci_op_get_next_capability,
    .get_auxdata = pci_op_get_auxdata,
    .get_bti = pci_op_get_bti,
};

// A device ops structure appears to be required still, but does not need
// to have any of the methods implemented. All of the proxy's work is done
// in its protocol methods.
static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
};

static zx_status_t pci_proxy_create(void* ctx, zx_device_t* parent,
                                    const char* name, const char* args,
                                    zx_handle_t rpcch) {
    if (!parent || !args) {
        return ZX_ERR_BAD_STATE;
    }

    uint32_t index = strtoul(args, NULL, 10);
    zx_pcie_device_info_t info;
    kpci_device_t* device = calloc(1, sizeof(kpci_device_t));
    if (!device) {
        return ZX_ERR_NO_MEMORY;
    }

    // The channel and index are all we need to make this protocol call and the
    // upper devhost is already fully initialized at this point so we can get
    // our bind information from it.
    device->index = index;
    device->pciroot_rpcch = rpcch;
    zx_status_t st = pci_op_get_device_info(device, &info);
    if (st != ZX_OK) {
        free(device);
        return st;
    }

    char devname[20];
    snprintf(devname, sizeof(devname), "%02x:%02x.%1x", info.bus_id, info.dev_id, info.func_id);
    device_add_args_t device_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = devname,
        .ctx = device,
        .ops = &device_ops,
        .proto_id = ZX_PROTOCOL_PCI,
        .proto_ops = &_pci_protocol,
    };

    st = device_add(parent, &device_args, &device->zxdev);
    if (st != ZX_OK) {
        free(device);
    }
    return st;
}

static zx_driver_ops_t kpci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .create = pci_proxy_create,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(pci_proxy, kpci_driver_ops, "zircon", "0.1", 1)
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(pci_proxy)
    // clang-format on
