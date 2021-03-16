// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/pci/hw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>
#include <zircon/process.h>

#include "src/devices/bus/drivers/pci/kpci/kpci-private.h"
#include "src/devices/bus/drivers/pci/pci_proxy_bind.h"
#include "src/devices/lib/pci/pci.h"

zx_status_t pci_rpc_request(kpci_device_t* dev, uint32_t op, zx_handle_t* handle, pci_msg_t* req,
                            pci_msg_t* resp) {
  if (dev->pciroot_rpcch == ZX_HANDLE_INVALID) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t in_handle_cnt = (op == PCI_OP_CONNECT_SYSMEM) ? 1 : 0;

  uint32_t handle_cnt = 0;
  if (handle) {
    // Since only the caller knows if they expected a valid handle back, make
    // sure the handle reads INVALID if we didn't get one.
    *handle = ZX_HANDLE_INVALID;
    handle_cnt = 1;
  }

  req->hdr.ordinal = op;
  zx_channel_call_args_t cc_args = {
      .wr_bytes = req,
      .rd_bytes = resp,
      .rd_handles = handle,
      .wr_num_bytes = sizeof(*req),
      .rd_num_bytes = sizeof(*resp),
      .rd_num_handles = handle_cnt,
      .wr_handles = in_handle_cnt ? &req->handle : NULL,
      .wr_num_handles = in_handle_cnt,
  };

  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t st = zx_channel_call(dev->pciroot_rpcch, 0, ZX_TIME_INFINITE, &cc_args, &actual_bytes,
                                   &actual_handles);
  if (st != ZX_OK) {
    return st;
  }

  if (actual_bytes != sizeof(*resp)) {
    return ZX_ERR_INTERNAL;
  }

  return (zx_status_t)resp->hdr.ordinal;
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
  if (width > sizeof(uint32_t) || val == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }

  pci_msg_t req = {
      .cfg =
          {
              .offset = offset,
              .width = (uint16_t)width,
          },
  };
  pci_msg_t resp = {};
  zx_status_t st = pci_rpc_request(dev, PCI_OP_CONFIG_READ, NULL, &req, &resp);
  if (st == ZX_OK) {
    *val = resp.cfg.value;
  }
  return st;
}

static zx_status_t pci_op_config_read8(void* ctx, uint16_t offset, uint8_t* val) {
  uint32_t tmp;
  zx_status_t st = pci_op_config_read(ctx, offset, sizeof(*val), &tmp);
  *val = tmp & UINT8_MAX;
  return st;
}

static zx_status_t pci_op_config_read16(void* ctx, uint16_t offset, uint16_t* val) {
  uint32_t tmp;
  zx_status_t st = pci_op_config_read(ctx, offset, sizeof(*val), &tmp);
  *val = tmp & UINT16_MAX;
  return st;
}

static zx_status_t pci_op_config_read32(void* ctx, uint16_t offset, uint32_t* val) {
  return pci_op_config_read(ctx, offset, sizeof(uint32_t), val);
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
      .cfg =
          {
              .offset = offset,
              .width = (uint16_t)width,
              .value = val,
          },
  };
  pci_msg_t resp = {};
  return pci_rpc_request(dev, PCI_OP_CONFIG_WRITE, NULL, &req, &resp);
}

static zx_status_t pci_op_config_write8(void* ctx, uint16_t offset, uint8_t val) {
  return pci_op_config_write(ctx, offset, sizeof(uint8_t), val);
}

static zx_status_t pci_op_config_write16(void* ctx, uint16_t offset, uint16_t val) {
  return pci_op_config_write(ctx, offset, sizeof(uint16_t), val);
}

static zx_status_t pci_op_config_write32(void* ctx, uint16_t offset, uint32_t val) {
  return pci_op_config_write(ctx, offset, sizeof(uint32_t), val);
}

static zx_status_t pci_op_get_next_capability(void* ctx, uint8_t type, uint8_t in_offset,
                                              uint8_t* out_offset) {
  uint32_t cap_offset = 0;
  pci_op_config_read(ctx, in_offset + 1, sizeof(uint8_t), &cap_offset);
  uint8_t limit = 64;
  zx_status_t st;

  // Walk the capability list looking for the type requested, starting at the offset
  // passed in. limit acts as a barrier in case of an invalid capability pointer list
  // that causes us to iterate forever otherwise.
  while (cap_offset != 0 && limit--) {
    uint32_t type_id = 0;
    if ((st = pci_op_config_read(ctx, (uint16_t)cap_offset, sizeof(uint8_t), &type_id)) != ZX_OK) {
      zxlogf(ERROR, "%s: error reading type from cap offset %#x: %d", __func__, cap_offset, st);
      return st;
    }

    if (type_id == type) {
      *out_offset = (uint8_t)cap_offset;
      return ZX_OK;
    }

    // We didn't find the right type, move on, but ensure we're still
    // within the first 256 bytes of standard config space.
    if (cap_offset >= UINT8_MAX) {
      zxlogf(ERROR, "%s: %#x is an invalid capability offset!", __func__, cap_offset);
      return ZX_ERR_BAD_STATE;
    }
    if ((st = pci_op_config_read(ctx, (uint16_t)(cap_offset + 1), sizeof(uint8_t), &cap_offset)) !=
        ZX_OK) {
      zxlogf(ERROR, "%s: error reading next cap from cap offset %#x: %d", __func__, cap_offset + 1,
             st);
      return ZX_ERR_BAD_STATE;
    }
  }

  // No more entries are in the list
  return ZX_ERR_NOT_FOUND;
}

static zx_status_t pci_op_get_first_capability(void* ctx, uint8_t type, uint8_t* out_offset) {
  // the next_capability method will always look at the second byte next
  // pointer to fetch the next capability. By offsetting the CapPtr field
  // by -1 we can pretend we're working with a normal capability entry
  return pci_op_get_next_capability(ctx, type, PCI_CFG_CAPABILITIES_PTR - 1u, out_offset);
}

static zx_status_t pci_op_get_bar(void* ctx, uint32_t bar_id, pci_bar_t* out_bar) {
  kpci_device_t* dev = ctx;
  pci_msg_t req = {
      .bar.id = bar_id,
  };
  pci_msg_t resp = {};
  zx_handle_t handle;
  zx_status_t st = pci_rpc_request(dev, PCI_OP_GET_BAR, &handle, &req, &resp);

  if (st == ZX_OK) {
    // Grab the payload and copy the handle over if one was passed back to us
    zx_pci_bar_to_banjo(resp.bar, out_bar);
    // *out_bar = resp.bar;

    if (out_bar->type == ZX_PCI_BAR_TYPE_PIO) {
#if __x86_64__
      // x86 PIO space access requires permission in the I/O bitmap
      // TODO: this is the last remaining use of get_root_resource in pci
      // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
      st = zx_ioports_request(get_root_resource(), (uint16_t)out_bar->u.addr,
                              (uint16_t)out_bar->size);
      if (st != ZX_OK) {
        zxlogf(ERROR, "Failed to map IO window for bar into process: %d", st);
        return st;
      }
#else
      zxlogf(INFO,
             "%s: PIO bars may not be supported correctly on this arch. "
             "Please have someone check this!\n",
             __func__);
#endif
    } else {
      out_bar->u.handle = handle;
    }
  }
  return st;
}

static zx_status_t pci_op_map_interrupt(void* ctx, uint32_t which_irq, zx_handle_t* out_handle) {
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

static zx_status_t pci_op_ack_interrupt(void* ctx) { return ZX_ERR_NOT_SUPPORTED; }

static zx_status_t pci_op_get_bti(void* ctx, uint32_t index, zx_handle_t* out_handle) {
  if (!out_handle) {
    return ZX_ERR_INVALID_ARGS;
  }

  kpci_device_t* dev = ctx;
  pci_msg_t req = {.bti_index = index};
  pci_msg_t resp = {};
  zx_handle_t handle;
  zx_status_t st = pci_rpc_request(dev, PCI_OP_GET_BTI, &handle, &req, &resp);
  if (st == ZX_OK) {
    *out_handle = handle;
  }
  return st;
}

static zx_status_t pci_op_query_irq_mode(void* ctx, pci_irq_mode_t mode, uint32_t* out_max_irqs) {
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

static zx_status_t pci_op_set_irq_mode(void* ctx, pci_irq_mode_t mode,
                                       uint32_t requested_irq_count) {
  kpci_device_t* dev = ctx;
  pci_msg_t req = {
      .irq =
          {
              .mode = mode,
              .requested_irqs = requested_irq_count,
          },
  };
  pci_msg_t resp = {};
  return pci_rpc_request(dev, PCI_OP_SET_IRQ_MODE, NULL, &req, &resp);
}

static zx_status_t pci_op_configure_irq_mode(void* ctx, uint32_t requested_irq_count,
                                             pci_irq_mode_t* mode) {
  kpci_device_t* dev = ctx;
  pci_msg_t req = {
      .irq =
          {
              .requested_irqs = requested_irq_count,
          },
  };
  pci_msg_t resp = {};
  zx_status_t st = pci_rpc_request(dev, PCI_OP_CONFIGURE_IRQ_MODE, NULL, &req, &resp);
  if (st == ZX_OK && mode) {
    *mode = resp.irq.mode;
  }
  return st;
}

static zx_status_t pci_op_get_device_info(void* ctx, pcie_device_info_t* out_info) {
  kpci_device_t* dev = ctx;
  pci_msg_t req = {};
  pci_msg_t resp = {};
  zx_status_t st = pci_rpc_request(dev, PCI_OP_GET_DEVICE_INFO, NULL, &req, &resp);
  if (st == ZX_OK) {
    zx_pci_device_info_to_banjo(resp.info, out_info);
  }
  return st;
}

static pci_protocol_ops_t _pci_protocol = {
    .enable_bus_master = pci_op_enable_bus_master,
    .reset_device = pci_op_reset_device,
    .get_bar = pci_op_get_bar,
    .map_interrupt = pci_op_map_interrupt,
    .ack_interrupt = pci_op_ack_interrupt,
    .configure_irq_mode = pci_op_configure_irq_mode,
    .query_irq_mode = pci_op_query_irq_mode,
    .set_irq_mode = pci_op_set_irq_mode,
    .get_device_info = pci_op_get_device_info,
    .config_read8 = pci_op_config_read8,
    .config_read16 = pci_op_config_read16,
    .config_read32 = pci_op_config_read32,
    .config_write8 = pci_op_config_write8,
    .config_write16 = pci_op_config_write16,
    .config_write32 = pci_op_config_write32,
    .get_next_capability = pci_op_get_next_capability,
    .get_first_capability = pci_op_get_first_capability,
    .get_bti = pci_op_get_bti,
};

static zx_status_t pci_sysmem_connect(void* ctx, zx_handle_t handle) {
  kpci_device_t* dev = ctx;
  pci_msg_t req = {
      .handle = handle,
  };
  pci_msg_t resp = {};
  return pci_rpc_request(dev, PCI_OP_CONNECT_SYSMEM, NULL, &req, &resp);
};

static sysmem_protocol_ops_t sysmem_protocol = {
    .connect = pci_sysmem_connect,
};

static zx_status_t get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
  if (proto_id == ZX_PROTOCOL_SYSMEM) {
    sysmem_protocol_t* proto = protocol;
    proto->ctx = ctx;
    proto->ops = &sysmem_protocol;
    return ZX_OK;
  } else if (proto_id == ZX_PROTOCOL_PCI) {
    pci_protocol_t* proto = protocol;
    proto->ctx = ctx;
    proto->ops = &_pci_protocol;
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

// A device ops structure appears to be required still, but does not need
// to have any of the methods implemented. All of the proxy's work is done
// in its protocol methods.
static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = get_protocol,
};

static zx_status_t pci_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                    const char* args, zx_handle_t rpcch) {
  if (!parent || !args) {
    return ZX_ERR_BAD_STATE;
  }

  unsigned long index = strtoul(args, NULL, 10);
  pcie_device_info_t info;
  kpci_device_t* device = calloc(1, sizeof(kpci_device_t));
  if (!device) {
    return ZX_ERR_NO_MEMORY;
  }

  if (index > UINT32_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }

  // The channel and index are all we need to make this protocol call and the
  // upper devhost is already fully initialized at this point so we can get
  // our bind information from it.
  device->index = (uint32_t)index;
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

ZIRCON_DRIVER(pci_proxy, kpci_driver_ops, "zircon", "0.1");
