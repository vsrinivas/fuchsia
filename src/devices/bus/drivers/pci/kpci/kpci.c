// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <lib/fidl/txn_header.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/pci.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <hw/pci.h>

#include "src/devices/bus/drivers/pci/kpci/kpci-private.h"
#include "src/devices/bus/drivers/pci/pci_bind.h"

#define KPCIDBG(f, ...) zxlogf(DEBUG, "%s: " f, __func__, ##__VA_ARGS__)
#define KPCIERR(f, ...) zxlogf(ERROR, "%s: " f, __func__, ##__VA_ARGS__)

// Convenience reply methods.
static zx_status_t pci_rpc_reply(zx_handle_t ch, zx_status_t status, zx_handle_t* handle,
                                 pci_msg_t* req, pci_msg_t* resp) {
  // If status isn't ZX_OK then it is immediately returned to be
  // handled by the rpc callback
  if (status != ZX_OK) {
    return status;
  }

  uint32_t handle_cnt = 0;
  if (handle && *handle != ZX_HANDLE_INVALID) {
    handle_cnt++;
  }

  fidl_init_txn_header(&resp->hdr, req->hdr.txid, status);
  return zx_channel_write(ch, 0, resp, sizeof(*resp), handle, handle_cnt);
};

// kpci is a driver that communicates with the kernel to publish a list of pci devices.
static zx_status_t kpci_enable_bus_master(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  pci_msg_t resp = {};
  zx_status_t st = zx_pci_enable_bus_master(device->handle, req->enable);
  return pci_rpc_reply(ch, st, NULL, req, &resp);
}

static zx_status_t kpci_reset_device(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  pci_msg_t resp = {};
  zx_status_t st = zx_pci_reset_device(device->handle);
  return pci_rpc_reply(ch, st, NULL, req, &resp);
}

// Reads from a config space address for a given device handle. Most of the heavy lifting
// is offloaded to the zx_pci_config_read syscall itself, and the rpc client that
// formats the arguments.
static zx_status_t kpci_config_read(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  uint32_t value = 0;
  pci_msg_t resp = {};
  zx_status_t st = zx_pci_config_read(device->handle, req->cfg.offset, req->cfg.width, &value);
  if (st == ZX_OK) {
    resp.cfg.offset = req->cfg.offset;
    resp.cfg.width = req->cfg.width;
    resp.cfg.value = value;
  }
  return pci_rpc_reply(ch, st, NULL, req, &resp);
}

static zx_status_t kpci_config_write(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  pci_msg_t resp = {};
  zx_status_t st =
      zx_pci_config_write(device->handle, req->cfg.offset, req->cfg.width, req->cfg.value);
  if (st == ZX_OK) {
    resp.cfg = req->cfg;
  }
  return pci_rpc_reply(ch, st, NULL, req, &resp);
}

// Retrieves either address information for PIO or a VMO corresponding to a device's
// bar to pass back to the devhost making the call.
static zx_status_t kpci_get_bar(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  if (req->bar.id >= ZX_PCI_MAX_BAR_REGS) {
    return ZX_ERR_INVALID_ARGS;
  }

  pci_msg_t resp = {};
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_pci_bar_t bar;
  zx_status_t st = zx_pci_get_bar(device->handle, req->bar.id, &bar, &handle);
  if (st == ZX_OK) {
    resp.bar = bar;
  }
  return pci_rpc_reply(ch, st, &handle, req, &resp);
}

static zx_status_t kpci_query_irq_mode(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  uint32_t max_irqs;
  pci_msg_t resp = {};
  zx_status_t st = zx_pci_query_irq_mode(device->handle, req->irq.mode, &max_irqs);
  if (st == ZX_OK) {
    resp.irq.mode = req->irq.mode;
    resp.irq.max_irqs = max_irqs;
  }
  return pci_rpc_reply(ch, st, NULL, req, &resp);
}

static zx_status_t kpci_set_irq_mode(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  pci_msg_t resp = {};
  zx_status_t st = zx_pci_set_irq_mode(device->handle, req->irq.mode, req->irq.requested_irqs);
  return pci_rpc_reply(ch, st, NULL, req, &resp);
}

static zx_status_t kpci_configure_irq_mode(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  pci_msg_t resp = {};

  // Walk the available IRQ mdoes from best to worst (from a system
  // perspective): MSI -> Legacy. Enable the mode that can provide the
  // number of interrupts requested. This enables drivers that don't care about
  // how they get their interrupt to call one method rather than doing the
  // QueryIrqMode/SetIrqMode dance.
  // TODO(fxbug.dev/32978): This method only covers MSI/Legacy because the transition to MSI-X
  // requires the userspace driver. When that happens, this code will go away.
  zx_pci_irq_mode_t mode = ZX_PCIE_IRQ_MODE_MSI;
  zx_status_t st = zx_pci_set_irq_mode(device->handle, mode, req->irq.requested_irqs);
  if (st != ZX_OK) {
    mode = ZX_PCIE_IRQ_MODE_LEGACY;
    st = zx_pci_set_irq_mode(device->handle, mode, req->irq.requested_irqs);
  }
  KPCIDBG("{ irq mode = %u, requested irqs = %u, status = %u }", mode, req->irq.requested_irqs, st);
  return pci_rpc_reply(ch, st, NULL, req, &resp);
}

static zx_status_t kpci_map_interrupt(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  pci_msg_t resp = {};
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t st = zx_pci_map_interrupt(device->handle, req->irq.which_irq, &handle);
  return pci_rpc_reply(ch, st, &handle, req, &resp);
}

static zx_status_t kpci_get_device_info(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  pci_msg_t resp = {
      .info = device->info,
  };
  return pci_rpc_reply(ch, ZX_OK, NULL, req, &resp);
}

static zx_status_t kpci_get_bti(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  // TODO(cja): Bring convenience functions/macros into a public header for
  // stuff like this.
  uint32_t bdf = ((uint32_t)device->info.bus_id << 8) | ((uint32_t)device->info.dev_id << 3) |
                 device->info.func_id;
  zx_handle_t bti;
  if (device->pciroot.ops) {
    zx_status_t status = pciroot_get_bti(&device->pciroot, bdf, req->bti_index, &bti);
    if (status != ZX_OK) {
      return status;
    }
  } else if (device->pdev.ops) {
    // TODO(teisenbe): This isn't quite right.  We need to develop a way to
    // resolve which BTI should go to downstream.  However, we don't
    // currently support any SMMUs for ARM, so this will work for now.
    zx_status_t status = pdev_get_bti(&device->pdev, 0, &bti);
    if (status != ZX_OK) {
      return status;
    }
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  pci_msg_t resp = {};
  return pci_rpc_reply(ch, ZX_OK, &bti, req, &resp);
}

static zx_status_t kpci_connect_sysmem(pci_msg_t* req, kpci_device_t* device, zx_handle_t ch) {
  pci_msg_t resp = {};
  if (device->pciroot.ops) {
    zx_status_t status = pciroot_connect_sysmem(&device->pciroot, req->handle);
    if (status != ZX_OK) {
      return status;
    }
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return pci_rpc_reply(ch, ZX_OK, NULL, req, &resp);
}

// All callbacks corresponding to protocol operations match this signature.
// Rather than passing the outgoing message back to kpci_rxrpc, the callback
// itself is expected to write to the channel directly. This greatly simplifies
// lifecycles around handles that need to be passed to/from the proxy devhost,
// as well as keeping the method declaration simpler. In the event of an error
// the callback can return the error code back to kpci_rxrpc and it will handle
// sending it back over the channel.
typedef zx_status_t (*rxrpc_cbk_t)(pci_msg_t*, kpci_device_t*, zx_handle_t);
const rxrpc_cbk_t rxrpc_cbk_tbl[] = {
    [PCI_OP_RESET_DEVICE] = kpci_reset_device,
    [PCI_OP_ENABLE_BUS_MASTER] = kpci_enable_bus_master,
    [PCI_OP_CONFIG_READ] = kpci_config_read,
    [PCI_OP_CONFIG_WRITE] = kpci_config_write,
    [PCI_OP_GET_BAR] = kpci_get_bar,
    [PCI_OP_QUERY_IRQ_MODE] = kpci_query_irq_mode,
    [PCI_OP_SET_IRQ_MODE] = kpci_set_irq_mode,
    [PCI_OP_CONFIGURE_IRQ_MODE] = kpci_configure_irq_mode,
    [PCI_OP_MAP_INTERRUPT] = kpci_map_interrupt,
    [PCI_OP_GET_DEVICE_INFO] = kpci_get_device_info,
    [PCI_OP_GET_BTI] = kpci_get_bti,
    [PCI_OP_CONNECT_SYSMEM] = kpci_connect_sysmem,
    [PCI_OP_MAX] = NULL,
};

#define LABEL(x) [x] = #x
const char* const rxrpc_string_tbl[] = {
    LABEL(PCI_OP_INVALID),        LABEL(PCI_OP_RESET_DEVICE),    LABEL(PCI_OP_ENABLE_BUS_MASTER),
    LABEL(PCI_OP_CONFIG_READ),    LABEL(PCI_OP_CONFIG_WRITE),    LABEL(PCI_OP_CONFIGURE_IRQ_MODE),
    LABEL(PCI_OP_GET_BAR),        LABEL(PCI_OP_QUERY_IRQ_MODE),  LABEL(PCI_OP_SET_IRQ_MODE),
    LABEL(PCI_OP_MAP_INTERRUPT),  LABEL(PCI_OP_GET_DEVICE_INFO), LABEL(PCI_OP_GET_BTI),
    LABEL(PCI_OP_CONNECT_SYSMEM),
};
#undef LABEL
static_assert(countof(rxrpc_string_tbl) == PCI_OP_MAX, "rpc string table is not contiguous!");

static inline const char* rpc_op_lbl(size_t op) {
  if (op >= PCI_OP_MAX) {
    return "<<INVALID OP>>";
  }
  return rxrpc_string_tbl[op];
}

static zx_status_t kpci_rxrpc(void* ctx, zx_handle_t ch) {
  if (ch == ZX_HANDLE_INVALID) {
    // new proxy connection
    return ZX_OK;
  }

  kpci_device_t* device = ctx;
  const char* name = device_get_name(device->zxdev);
  pci_msg_t req = {};
  uint32_t actual_bytes;
  zx_handle_t handle = ZX_HANDLE_INVALID;
  uint32_t actual_handles;
  zx_status_t st =
      zx_channel_read(ch, 0, &req, &handle, sizeof(req), 1, &actual_bytes, &actual_handles);
  if (st != ZX_OK) {
    KPCIERR("pci[%s]: error reading from channel %d", name, st);
    return st;
  }

  if (actual_bytes != sizeof(req)) {
    zx_handle_close(handle);
    KPCIERR("pci[%s]: channel read size invalid!", name);
    return ZX_ERR_INTERNAL;
  }

  uint64_t op = req.hdr.ordinal;
  uint32_t id = req.hdr.txid;
  if (op >= PCI_OP_MAX || rxrpc_cbk_tbl[op] == NULL) {
    KPCIERR("pci[%s]: unsupported rpc op %zu", name, op);
    st = ZX_ERR_NOT_SUPPORTED;
    goto err;
  }
  if (req.hdr.ordinal == PCI_OP_CONNECT_SYSMEM) {
    if (actual_handles > 0) {
      req.handle = handle;
      handle = ZX_HANDLE_INVALID;
    } else {
      st = ZX_ERR_INVALID_ARGS;
      goto err;
    }
  }

  KPCIDBG("pci[%s]: rpc = %#x, op = %s(%zu) args '%#02x %#02x %#02x %#02x...'", name, id,
          rpc_op_lbl(op), op, req.data[0], req.data[1], req.data[2], req.data[3]);
  st = rxrpc_cbk_tbl[req.hdr.ordinal](&req, device, ch);
  if (st != ZX_OK) {
    goto err;
  }

  KPCIDBG("pci[%s]: rpc = %#x, op = %s(%zu) ZX_OK", name, id, rpc_op_lbl(op), op);
  return st;

err:;
  pci_msg_t resp = {
      .hdr = {},  // initialized below
  };
  fidl_init_txn_header(&resp.hdr, req.hdr.txid, st);
  zx_handle_close(handle);

  KPCIDBG("pci[%s]: rpc = %#x, op = %s(%zu) error %d", name, id, rpc_op_lbl(op), op, st);
  return zx_channel_write(ch, 0, &resp, sizeof(resp), NULL, 0);
}

static void kpci_release(void* ctx) {
  kpci_device_t* device = ctx;
  if (device->handle != ZX_HANDLE_INVALID) {
    zx_handle_close(device->handle);
  }
  free(device);
}

static zx_protocol_device_t pci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .rxrpc = kpci_rxrpc,
    .release = kpci_release,
};

// Initializes the upper half of a pci / pci.proxy devhost pair.
static zx_status_t pci_init_child(zx_device_t* parent, uint32_t index) {
  zx_pcie_device_info_t info;
  zx_handle_t handle;

  if (!parent) {
    return ZX_ERR_BAD_STATE;
  }

  // TODO: What is an 'nth' device in a world where a device may be added/removed via hotplug?
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
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
  device->info = info;
  device->handle = handle;
  device->index = index;

  // Store the PCIROOT protocol for use with get_bti in the pci protocol
  // It is not fatal if this fails, but bti protocol methods will not work.
  device_get_protocol(parent, ZX_PROTOCOL_PCIROOT, &device->pciroot);
  device_get_protocol(parent, ZX_PROTOCOL_PDEV, &device->pdev);

  char name[20];
  snprintf(name, sizeof(name), "%02x:%02x.%1x", info.bus_id, info.dev_id, info.func_id);
  zx_device_prop_t device_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, info.vendor_id},
      {BIND_PCI_DID, 0, info.device_id},
      {BIND_PCI_CLASS, 0, info.base_class},
      {BIND_PCI_SUBCLASS, 0, info.sub_class},
      {BIND_PCI_INTERFACE, 0, info.program_interface},
      {BIND_PCI_REVISION, 0, info.revision_id},
      {BIND_TOPO_PCI, 0, BIND_TOPO_PCI_PACK(info.bus_id, info.dev_id, info.func_id)},
  };

  // The most important detail here is the handling of DEVICE_ADD_MUST_ISOLATE. With that
  // flag passed to devmgr it will create the bottom half devhost and attempt to load the
  // proxy by looking for pci.proxy.so. It calls the create() hook defined in that driver,
  // which is implemented in system/dev/bus/pci/proxy.c.
  char argstr[64];
  snprintf(argstr, sizeof(argstr), "pci#%u:%04x:%04x,%u", index, info.vendor_id, info.device_id,
           index);
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = name,
      .ctx = device,
      .ops = &pci_device_proto,
      .proto_id = ZX_PROTOCOL_PCI,
      .props = device_props,
      .prop_count = countof(device_props),
      .proxy_args = argstr,
      .flags = DEVICE_ADD_MUST_ISOLATE,
  };

  status = device_add(parent, &args, &device->zxdev);
  if (status != ZX_OK) {
    zx_handle_close(handle);
    free(device);
  }

  return status;
}

static zx_status_t pci_drv_bind(void* ctx, zx_device_t* parent) {
  // Walk PCI devices to create their upper half devices until we hit the end
  for (uint32_t index = 0;; index++) {
    if (pci_init_child(parent, index) != ZX_OK) {
      break;
    }
  }
  return ZX_OK;
}

static zx_driver_ops_t kpci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pci_drv_bind,
};

ZIRCON_DRIVER(pci, kpci_driver_ops, "zircon", "0.1");
