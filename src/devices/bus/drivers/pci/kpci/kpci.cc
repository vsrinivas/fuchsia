// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/pci/hw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/pci/device_rpc.h"
#include "src/devices/bus/drivers/pci/pci_bind.h"

struct kpci_device {
  zx_device_t* zxdev;
  // only set for non-proxy devices
  pciroot_protocol_t pciroot;
  pdev_protocol_t pdev;
  // only set for proxy devices
  zx_handle_t pciroot_rpcch;
  // kernel pci handle, only set for shadow devices
  zx_handle_t handle;
  // nth device index
  uint32_t index;
  zx_pcie_device_info_t info;
};

namespace pci {
// Convenience reply methods.
static zx_status_t pci_rpc_reply(zx_handle_t& ch, zx_status_t status, zx_handle_t* handle,
                                 PciRpcMsg* req, PciRpcMsg* resp) {
  uint32_t handle_cnt = 0;
  if (handle && *handle != ZX_HANDLE_INVALID) {
    handle_cnt++;
  }

  resp->op = req->op;
  resp->txid = req->txid;
  resp->ret = status;
  zxlogf(TRACE, "[%#x] --> op %u txid %#x = %s", ch, resp->op, resp->txid,
         zx_status_get_string(resp->ret));
  return zx_channel_write(ch, 0, resp, sizeof(PciRpcMsg), handle, handle_cnt);
}

// kpci is a driver that communicates with the kernel to publish a list of pci devices.
static zx_status_t kpci_enable_bus_master(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                          PciRpcMsg* resp) {
  zx_status_t st = zx_pci_enable_bus_master(device->handle, req->enable);
  return pci_rpc_reply(ch, st, nullptr, req, resp);
}

static zx_status_t kpci_reset_device(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                     PciRpcMsg* resp) {
  zx_status_t st = zx_pci_reset_device(device->handle);
  return pci_rpc_reply(ch, st, nullptr, req, resp);
}

// Reads from a config space address for a given device handle. Most of the
// heavy lifting is offloaded to the zx_pci_config_read syscall itself, and the
// rpc client that formats the arguments.
static zx_status_t kpci_config_read(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                    PciRpcMsg* resp) {
  uint32_t value = 0;
  zx_status_t st = zx_pci_config_read(device->handle, req->cfg.offset, req->cfg.width, &value);
  if (st == ZX_OK) {
    resp->cfg.offset = req->cfg.offset;
    resp->cfg.width = req->cfg.width;
    resp->cfg.value = value;
  }
  return pci_rpc_reply(ch, st, nullptr, req, resp);
}

static zx_status_t kpci_config_write(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                     PciRpcMsg* resp) {
  zx_status_t st =
      zx_pci_config_write(device->handle, req->cfg.offset, req->cfg.width, req->cfg.value);
  if (st == ZX_OK) {
    resp->cfg = req->cfg;
  }
  return pci_rpc_reply(ch, st, nullptr, req, resp);
}

// Retrieves either address information for PIO or a VMO corresponding to a
// device's bar to pass back to the devhost making the call.
static zx_status_t kpci_get_bar(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                PciRpcMsg* resp) {
  if (req->bar.id >= ZX_PCI_MAX_BAR_REGS) {
    return pci_rpc_reply(ch, ZX_ERR_INVALID_ARGS, nullptr, req, resp);
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_pci_bar_t bar{};
  zx_status_t st = zx_pci_get_bar(device->handle, req->bar.id, &bar, &handle);
  if (st == ZX_OK) {
    resp->bar = {
        .id = bar.id,
        .is_mmio = (bar.type == ZX_PCI_BAR_TYPE_MMIO),
        .size = bar.size,
        .io_addr = bar.addr,
    };

    if (!resp->bar.is_mmio) {
      const char name[] = "kPCI IO";
      st = zx_resource_create(get_root_resource(), ZX_RSRC_KIND_IOPORT, resp->bar.io_addr,
                              resp->bar.size, name, sizeof(name), &handle);
      if (st != ZX_OK) {
        return pci_rpc_reply(ch, st, nullptr, req, resp);
      }
    }
  }
  return pci_rpc_reply(ch, st, &handle, req, resp);
}

static zx_status_t kpci_query_irq_mode(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                       PciRpcMsg* resp) {
  uint32_t max_irqs;
  zx_status_t st = zx_pci_query_irq_mode(device->handle, req->irq.mode, &max_irqs);
  if (st == ZX_OK) {
    resp->irq.mode = req->irq.mode;
    resp->irq.max_irqs = max_irqs;
  }
  return pci_rpc_reply(ch, st, nullptr, req, resp);
}

static zx_status_t kpci_set_irq_mode(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                     PciRpcMsg* resp) {
  zx_status_t st = zx_pci_set_irq_mode(device->handle, req->irq.mode, req->irq.requested_irqs);
  return pci_rpc_reply(ch, st, nullptr, req, resp);
}

static zx_status_t kpci_configure_irq_mode(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                           PciRpcMsg* resp) {
  // Walk the available IRQ mdoes from best to worst (from a system
  // perspective): MSI -> Legacy. Enable the mode that can provide the number of
  // interrupts requested. This enables drivers that don't care about how they
  // get their interrupt to call one method rather than doing the
  // QueryIrqMode/SetIrqMode dance. TODO(fxbug.dev/32978): This method only
  // covers MSI/Legacy because the transition to MSI-X requires the userspace
  // driver. When that happens, this code will go away.
  zx_pci_irq_mode_t mode = ZX_PCIE_IRQ_MODE_MSI;
  zx_status_t st = zx_pci_set_irq_mode(device->handle, mode, req->irq.requested_irqs);
  if (st != ZX_OK) {
    mode = ZX_PCIE_IRQ_MODE_LEGACY;
    st = zx_pci_set_irq_mode(device->handle, mode, req->irq.requested_irqs);
  }

  if (st == ZX_OK) {
    resp->irq.mode = mode;
  }
  return pci_rpc_reply(ch, st, nullptr, req, resp);
}

static zx_status_t kpci_get_next_capability(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                            PciRpcMsg* resp) {
  uint32_t starting_offset = (req->cap.is_first) ? PCI_CFG_CAPABILITIES_PTR : req->cap.offset + 1;
  uint32_t cap_offset;
  uint8_t limit = 64;
  zx_status_t st;

  // Walk the capability list looking for the type requested, starting at the offset
  // passed in. limit acts as a barrier in case of an invalid capability pointer list
  // that causes us to iterate forever otherwise.
  zx_pci_config_read(device->handle, starting_offset, sizeof(uint8_t), &cap_offset);
  while (cap_offset != 0 && limit--) {
    uint32_t type_id = 0;
    if ((st = zx_pci_config_read(device->handle, static_cast<uint16_t>(cap_offset), sizeof(uint8_t),
                                 &type_id)) != ZX_OK) {
      zxlogf(ERROR, "%s: error reading type from cap offset %#x: %d", __func__, cap_offset, st);
      return pci_rpc_reply(ch, st, nullptr, req, resp);
    }

    if (type_id == req->cap.id) {
      resp->cap.offset = static_cast<uint8_t>(cap_offset);
      return pci_rpc_reply(ch, ZX_OK, nullptr, req, resp);
    }

    // We didn't find the right type, move on, but ensure we're still within the
    // first 256 bytes of standard config space.
    if (cap_offset >= UINT8_MAX) {
      zxlogf(ERROR, "%s: %#x is an invalid capability offset!", __func__, cap_offset);
      break;
    }
    if ((st = zx_pci_config_read(device->handle, static_cast<uint16_t>(cap_offset + 1),
                                 sizeof(uint8_t), &cap_offset)) != ZX_OK) {
      zxlogf(ERROR, "%s: error reading next cap from cap offset %#x: %d", __func__, cap_offset + 1,
             st);
      break;
    }
  }
  return pci_rpc_reply(ch, ZX_ERR_BAD_STATE, nullptr, req, resp);
}

static zx_status_t kpci_map_interrupt(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                      PciRpcMsg* resp) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t st = zx_pci_map_interrupt(device->handle, req->irq.which_irq, &handle);
  return pci_rpc_reply(ch, st, &handle, req, resp);
}

static zx_status_t kpci_ack_interrupt(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                      PciRpcMsg* resp) {
  zx_status_t st = ZX_OK;
  return pci_rpc_reply(ch, st, nullptr, req, resp);
}

static zx_status_t kpci_get_device_info(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                        PciRpcMsg* resp) {
  memcpy(&resp->info, &device->info, sizeof(resp->info));
  return pci_rpc_reply(ch, ZX_OK, nullptr, req, resp);
}

static zx_status_t kpci_get_bti(zx_handle_t ch, kpci_device* device, PciRpcMsg* req,
                                PciRpcMsg* resp) {
  uint32_t bdf = (static_cast<uint32_t>(device->info.bus_id) << 8) |
                 (static_cast<uint32_t>(device->info.dev_id) << 3) | device->info.func_id;
  zx_handle_t bti;
  if (device->pciroot.ops) {
    zx_status_t status = pciroot_get_bti(&device->pciroot, bdf, req->bti_index, &bti);
    if (status != ZX_OK) {
      return status;
    }
  } else if (device->pdev.ops) {
    // TODO(teisenbe): This isn't quite right. We need to develop a way to
    // resolve which BTI should go to downstream. However, we don't currently
    // support any SMMUs for ARM, so this will work for now.
    zx_status_t status = pdev_get_bti(&device->pdev, 0, &bti);
    if (status != ZX_OK) {
      return status;
    }
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return pci_rpc_reply(ch, ZX_OK, &bti, req, resp);
}

static zx_status_t kpci_connect_sysmem(zx_handle_t& ch, kpci_device* device, zx_handle_t handle,
                                       PciRpcMsg* req, PciRpcMsg* resp) {
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  if (device->pciroot.ops) {
    status = pciroot_connect_sysmem(&device->pciroot, handle);
  }
  return pci_rpc_reply(ch, status, nullptr, req, resp);
}

static zx_status_t kpci_rxrpc(void* ctx, zx_handle_t ch) {
  PciRpcMsg request{};
  PciRpcMsg response{};
  if (ch == ZX_HANDLE_INVALID) {
    // A new connection has been made, there's nothing
    // else to do.
    return ZX_OK;
  }

  uint32_t bytes_in;
  uint32_t handles_in;
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t st =
      zx_channel_read(ch, 0, &request, &handle, sizeof(request), 1, &bytes_in, &handles_in);
  if (st != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }

  if (bytes_in != sizeof(request)) {
    return ZX_ERR_INTERNAL;
  }

  zxlogf(TRACE, "[%#x] <-- op %u txid %#x", ch, request.op, request.txid);
  auto* device = static_cast<kpci_device*>(ctx);
  switch (request.op) {
    case PCI_OP_CONFIG_READ:
      return kpci_config_read(ch, device, &request, &response);
    case PCI_OP_CONFIG_WRITE:
      return kpci_config_write(ch, device, &request, &response);
    case PCI_OP_CONFIGURE_IRQ_MODE:
      return kpci_configure_irq_mode(ch, device, &request, &response);
    case PCI_OP_CONNECT_SYSMEM:
      return kpci_connect_sysmem(ch, device, handle, &request, &response);
    case PCI_OP_ENABLE_BUS_MASTER:
      return kpci_enable_bus_master(ch, device, &request, &response);
    case PCI_OP_GET_BAR:
      return kpci_get_bar(ch, device, &request, &response);
    case PCI_OP_GET_BTI:
      return kpci_get_bti(ch, device, &request, &response);
    case PCI_OP_GET_DEVICE_INFO:
      return kpci_get_device_info(ch, device, &request, &response);
    case PCI_OP_GET_NEXT_CAPABILITY:
      return kpci_get_next_capability(ch, device, &request, &response);
    case PCI_OP_MAP_INTERRUPT:
      return kpci_map_interrupt(ch, device, &request, &response);
    case PCI_OP_QUERY_IRQ_MODE:
      return kpci_query_irq_mode(ch, device, &request, &response);
    case PCI_OP_RESET_DEVICE:
      return kpci_reset_device(ch, device, &request, &response);
    case PCI_OP_SET_IRQ_MODE:
      return kpci_set_irq_mode(ch, device, &request, &response);
    case PCI_OP_ACK_INTERRUPT:
      return kpci_ack_interrupt(ch, device, &request, &response);
    default:
      return pci_rpc_reply(ch, ZX_ERR_INVALID_ARGS, nullptr, &request, &response);
  };
}

static void kpci_release(void* ctx) {
  auto* device = static_cast<kpci_device*>(ctx);
  if (device->handle != ZX_HANDLE_INVALID) {
    zx_handle_close(device->handle);
  }
  free(device);
}

static zx_protocol_device_t pci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = kpci_release,
    .rxrpc = kpci_rxrpc,
};

// Initializes the upper half of a pci / pci.proxy devhost pair.
static zx_status_t pci_init_child(zx_device_t* parent, uint32_t index) {
  zx_pcie_device_info_t info;
  zx_handle_t handle;

  if (!parent) {
    return ZX_ERR_BAD_STATE;
  }

  // This is a legacy function to get the 'nth' device on a bus. Please do not
  // use get_root_resource() in new code. See fxbug.dev/31358.
  zx_status_t status = zx_pci_get_nth_device(get_root_resource(), index, &info, &handle);
  if (status != ZX_OK) {
    return status;
  }

  auto* device = static_cast<kpci_device*>(calloc(1, sizeof(kpci_device)));
  if (!device) {
    zx_handle_close(handle);
    return ZX_ERR_NO_MEMORY;
  }
  memcpy(&device->info, &info, sizeof(info));
  device->info = info;
  device->handle = handle;
  device->index = index;

  // Store the PCIROOT protocol for use with get_bti in the pci protocol It is
  // not fatal if this fails, but bti protocol methods will not work.
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
      {BIND_TOPO_PCI, 0,
       static_cast<uint32_t>(BIND_TOPO_PCI_PACK(info.bus_id, info.dev_id, info.func_id))},
  };

  // Spawn the PCI proxy driver with a handle to the PciDeviceDispatcher of the given device
  // for the Ddk context argument.
  char argstr[64];
  snprintf(argstr, sizeof(argstr), "pci#%u:%04x:%04x,%u", index, info.vendor_id, info.device_id,
           index);
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = name,
      .ctx = device,
      .ops = &pci_device_proto,
      .props = device_props,
      .prop_count = countof(device_props),
      .proto_id = ZX_PROTOCOL_PCI,
      .proxy_args = argstr,
      .flags = DEVICE_ADD_MUST_ISOLATE,
  };

  status = device_add(parent, &args, &device->zxdev);
  if (status != ZX_OK) {
    zx_handle_close(handle);
    free(device);
  }

  return status;
}  // namespace pci

static zx_status_t pci_drv_bind(void* ctx, zx_device_t* parent) {
  // Walk PCI devices to create their upper half devices until we hit the end
  for (uint32_t index = 0;; index++) {
    if (pci_init_child(parent, index) != ZX_OK) {
      break;
    }
  }
  return ZX_OK;
}

}  // namespace pci

static zx_driver_ops_t kpci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pci::pci_drv_bind,
};

ZIRCON_DRIVER(pci, kpci_driver_ops, "zircon", "0.1");
