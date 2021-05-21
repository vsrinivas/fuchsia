// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/drivers/pci/kpci.h"

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
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

#include <memory>

#include <bind/fuchsia/pci/cpp/fidl.h>
#include <ddktl/device.h>

#include "src/devices/bus/drivers/pci/pci_bind.h"
#include "src/devices/bus/drivers/pci/proxy_rpc.h"

namespace pci {

static const zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};

static const device_fragment_part_t sysmem_fragment[] = {
    {countof(sysmem_match), sysmem_match},
};

zx_status_t KernelPci::CreateComposite(zx_device_t* parent, kpci_device device) {
  zx_device_prop_t fragment_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, device.info.vendor_id},
      {BIND_PCI_DID, 0, device.info.device_id},
      {BIND_PCI_CLASS, 0, device.info.base_class},
      {BIND_PCI_SUBCLASS, 0, device.info.sub_class},
      {BIND_PCI_INTERFACE, 0, device.info.program_interface},
      {BIND_PCI_REVISION, 0, device.info.revision_id},
      {BIND_PCI_COMPONENT, 0, bind::fuchsia::pci::BIND_PCI_COMPONENT_COMPONENT},
      {BIND_PCI_TOPO, 0,
       static_cast<uint32_t>(
           BIND_PCI_TOPO_PACK(device.info.bus_id, device.info.dev_id, device.info.func_id))},
  };

  char name[ZX_DEVICE_NAME_MAX];
  snprintf(name, sizeof(device.name), "fragment %s", device.name);

  auto kpci = std::unique_ptr<KernelPci>(new KernelPci(parent, device));
  zx_status_t status = kpci->DdkAdd(
      ddk::DeviceAddArgs(name).set_props(fragment_props).set_proto_id(ZX_PROTOCOL_PCI));
  if (status != ZX_OK) {
    return status;
  }

  auto pci_bind_topo = static_cast<uint32_t>(
      BIND_PCI_TOPO_PACK(device.info.bus_id, device.info.dev_id, device.info.func_id));
  const zx_bind_inst_t pci_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
      BI_ABORT_IF(NE, BIND_PCI_VID, device.info.vendor_id),
      BI_ABORT_IF(NE, BIND_PCI_DID, device.info.device_id),
      BI_ABORT_IF(NE, BIND_PCI_CLASS, device.info.base_class),
      BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, device.info.sub_class),
      BI_ABORT_IF(NE, BIND_PCI_INTERFACE, device.info.program_interface),
      BI_ABORT_IF(NE, BIND_PCI_REVISION, device.info.revision_id),
      BI_ABORT_IF(NE, BIND_PCI_COMPONENT, bind::fuchsia::pci::BIND_PCI_COMPONENT_COMPONENT),
      BI_MATCH_IF(EQ, BIND_PCI_TOPO, pci_bind_topo),
  };

  const device_fragment_part_t pci_fragment[] = {
      {countof(pci_match), pci_match},
  };

  const device_fragment_t fragments[] = {
      {"sysmem", countof(sysmem_fragment), sysmem_fragment},
      {"pci", countof(pci_fragment), pci_fragment},
  };
  zx_device_prop_t composite_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, device.info.vendor_id},
      {BIND_PCI_DID, 0, device.info.device_id},
      {BIND_PCI_CLASS, 0, device.info.base_class},
      {BIND_PCI_SUBCLASS, 0, device.info.sub_class},
      {BIND_PCI_INTERFACE, 0, device.info.program_interface},
      {BIND_PCI_REVISION, 0, device.info.revision_id},
      {BIND_PCI_COMPONENT, 0, bind::fuchsia::pci::BIND_PCI_COMPONENT_COMPOSITE},
      {BIND_PCI_TOPO, 0,
       static_cast<uint32_t>(
           BIND_PCI_TOPO_PACK(device.info.bus_id, device.info.dev_id, device.info.func_id))},
  };

  composite_device_desc_t composite_desc = {
      .props = composite_props,
      .props_count = countof(composite_props),
      .fragments = fragments,
      .fragments_count = countof(fragments),
      .coresident_device_index = UINT32_MAX,  // create a new devhost
  };

  snprintf(name, sizeof(device.name), "composite %s", device.name);
  auto kpci_composite = std::unique_ptr<KernelPci>(new KernelPci(parent, device));
  status = kpci_composite->DdkAddComposite(name, &composite_desc);
  if (status != ZX_OK) {
    return status;
  }

  static_cast<void>(kpci_composite.release());
  static_cast<void>(kpci.release());
  return status;
}

zx_status_t KernelPci::CreateSimple(zx_device_t* parent, kpci_device device) {
  char argstr[ZX_DEVICE_NAME_MAX];
  snprintf(argstr, sizeof(argstr), "pci#%04x:%04x,%02x:%02x.%1x", device.info.vendor_id,
           device.info.device_id, device.info.bus_id, device.info.dev_id, device.info.func_id);

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, device.info.vendor_id},
      {BIND_PCI_DID, 0, device.info.device_id},
      {BIND_PCI_CLASS, 0, device.info.base_class},
      {BIND_PCI_SUBCLASS, 0, device.info.sub_class},
      {BIND_PCI_INTERFACE, 0, device.info.program_interface},
      {BIND_PCI_REVISION, 0, device.info.revision_id},
      {BIND_PCI_COMPONENT, 0, 0},
      {BIND_PCI_TOPO, 0,
       static_cast<uint32_t>(
           BIND_PCI_TOPO_PACK(device.info.bus_id, device.info.dev_id, device.info.func_id))},
  };

  auto kpci = std::unique_ptr<KernelPci>(new KernelPci(parent, device));
  zx_status_t status = kpci->DdkAdd(ddk::DeviceAddArgs(device.name)
                                        .set_props(props)
                                        .set_proto_id(ZX_PROTOCOL_PCI)
                                        .set_proxy_args(argstr)
                                        .set_flags(DEVICE_ADD_MUST_ISOLATE));
  if (status == ZX_OK) {
    static_cast<void>(kpci.release());
  }

  return status;
}

zx_status_t KernelPci::DdkGetProtocol(uint32_t proto_id, void* out) {
  switch (proto_id) {
    case ZX_PROTOCOL_PCI: {
      auto proto = static_cast<pci_protocol_t*>(out);
      proto->ctx = this;
      proto->ops = &pci_protocol_ops_;
      return ZX_OK;
    }
    case ZX_PROTOCOL_SYSMEM: {
      auto proto = static_cast<sysmem_protocol_t*>(out);
      proto->ctx = this;
      proto->ops = &sysmem_protocol_ops_;
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_SUPPORTED;
}
void KernelPci::DdkRelease() {
  if (device_.handle != ZX_HANDLE_INVALID) {
    zx_handle_close(device_.handle);
  }
}

zx_status_t KernelPci::DdkRxrpc(zx_handle_t ch) {
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

  switch (request.op) {
    case PCI_OP_CONFIG_READ:
      return RpcConfigRead(ch, &request, &response);
    case PCI_OP_CONFIG_WRITE:
      return RpcConfigWrite(ch, &request, &response);
    case PCI_OP_CONFIGURE_IRQ_MODE:
      return RpcConfigureIrqMode(ch, &request, &response);
    case PCI_OP_CONNECT_SYSMEM:
      return RpcConnectSysmem(ch, handle, &request, &response);
    case PCI_OP_ENABLE_BUS_MASTER:
      return RpcEnableBusMaster(ch, &request, &response);
    case PCI_OP_GET_BAR:
      return RpcGetBar(ch, &request, &response);
    case PCI_OP_GET_BTI:
      return RpcGetBti(ch, &request, &response);
    case PCI_OP_GET_DEVICE_INFO:
      return RpcGetDeviceInfo(ch, &request, &response);
    case PCI_OP_GET_NEXT_CAPABILITY:
      return RpcGetNextCapability(ch, &request, &response);
    case PCI_OP_MAP_INTERRUPT:
      return RpcMapInterrupt(ch, &request, &response);
    case PCI_OP_QUERY_IRQ_MODE:
      return RpcQueryIrqMode(ch, &request, &response);
    case PCI_OP_RESET_DEVICE:
      return RpcResetDevice(ch, &request, &response);
    case PCI_OP_SET_IRQ_MODE:
      return RpcSetIrqMode(ch, &request, &response);
    case PCI_OP_ACK_INTERRUPT:
      return RpcAckInterrupt(ch, &request, &response);
    default:
      return RpcReply(ch, ZX_ERR_INVALID_ARGS, nullptr, &request, &response);
  };
}

zx_status_t KernelPci::PciGetBar(uint32_t bar_id, pci_bar_t* out_res) {
  if (bar_id >= ZX_PCI_MAX_BAR_REGS) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_pci_bar_t bar{};
  zx_status_t st = zx_pci_get_bar(device_.handle, bar_id, &bar, &handle);
  if (st == ZX_OK) {
    out_res->id = bar_id;
    out_res->size = bar.size;
    out_res->type = bar.type;
    out_res->address = bar.addr;
    if (out_res->type == ZX_PCI_BAR_TYPE_PIO) {
      char name[] = "kPCI IO";
      st = zx_resource_create(get_root_resource(), ZX_RSRC_KIND_IOPORT, bar.addr, bar.size, name,
                              sizeof(name), &handle);
    }
    out_res->handle = handle;
  }

  return st;
}

zx_status_t KernelPci::PciEnableBusMaster(bool enable) {
  return zx_pci_enable_bus_master(device_.handle, enable);
}

zx_status_t KernelPci::PciResetDevice() { return zx_pci_reset_device(device_.handle); }

zx_status_t KernelPci::PciAckInterrupt() { return ZX_OK; }

zx_status_t KernelPci::PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle) {
  return zx_pci_map_interrupt(device_.handle, which_irq, out_handle->reset_and_get_address());
}

zx_status_t KernelPci::PciConfigureIrqMode(uint32_t requested_irq_count, pci_irq_mode_t* out_mode) {
  // Walk the available IRQ modes from best to worst (from a system
  // perspective): MSI -> Legacy. Enable the mode that can provide the number of
  // interrupts requested. This enables drivers that don't care about how they
  // get their interrupt to call one method rather than doing the
  // QueryIrqMode/SetIrqMode dance. TODO(fxbug.dev/32978): This method only
  // covers MSI/Legacy because the transition to MSI-X requires the userspace
  // driver. When that happens, this code will go away.
  zx_pci_irq_mode_t mode = ZX_PCIE_IRQ_MODE_MSI;
  zx_status_t st = zx_pci_set_irq_mode(device_.handle, mode, requested_irq_count);
  if (st != ZX_OK) {
    mode = ZX_PCIE_IRQ_MODE_LEGACY;
    st = zx_pci_set_irq_mode(device_.handle, mode, requested_irq_count);
  }

  if (st == ZX_OK) {
    *out_mode = mode;
    return st;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t KernelPci::PciQueryIrqMode(pci_irq_mode_t mode, uint32_t* out_max_irqs) {
  return zx_pci_query_irq_mode(device_.handle, mode, out_max_irqs);
}

zx_status_t KernelPci::PciSetIrqMode(pci_irq_mode_t mode, uint32_t requested_irq_count) {
  return zx_pci_set_irq_mode(device_.handle, mode, requested_irq_count);
}

zx_status_t KernelPci::PciGetDeviceInfo(pcie_device_info_t* out_info) {
  memcpy(out_info, &device_.info, sizeof(*out_info));
  return ZX_OK;
}

template <typename T>
zx_status_t ConfigRead(zx_handle_t device, uint16_t offset, T* out_value) {
  uint32_t value;
  zx_status_t st = zx_pci_config_read(device, offset, sizeof(T), &value);
  if (st == ZX_OK) {
    *out_value = static_cast<T>(value);
  }
  return st;
}

zx_status_t KernelPci::PciConfigRead8(uint16_t offset, uint8_t* out_value) {
  return ConfigRead(device_.handle, offset, out_value);
}

zx_status_t KernelPci::PciConfigRead16(uint16_t offset, uint16_t* out_value) {
  return ConfigRead(device_.handle, offset, out_value);
}

zx_status_t KernelPci::PciConfigRead32(uint16_t offset, uint32_t* out_value) {
  return ConfigRead(device_.handle, offset, out_value);
}
zx_status_t KernelPci::PciConfigWrite8(uint16_t offset, uint8_t value) {
  return zx_pci_config_write(device_.handle, offset, sizeof(value), value);
}

zx_status_t KernelPci::PciConfigWrite16(uint16_t offset, uint16_t value) {
  return zx_pci_config_write(device_.handle, offset, sizeof(value), value);
}

zx_status_t KernelPci::PciConfigWrite32(uint16_t offset, uint32_t value) {
  return zx_pci_config_write(device_.handle, offset, sizeof(value), value);
}

zx_status_t KernelPci::PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset) {
  return PciGetNextCapability(cap_id, PCI_CFG_CAPABILITIES_PTR, out_offset);
}

zx_status_t KernelPci::PciGetNextCapability(uint8_t cap_id, uint8_t offset, uint8_t* out_offset) {
  uint32_t cap_offset{};
  uint8_t limit = 64;
  zx_status_t st;

  // Walk the capability list looking for the type requested, starting at the offset
  // passed in. limit acts as a barrier in case of an invalid capability pointer list
  // that causes us to iterate forever otherwise.
  zx_pci_config_read(device_.handle, offset, sizeof(uint8_t), &cap_offset);
  while (cap_offset != 0 && cap_offset != 0xFF && limit--) {
    uint32_t type_id = 0;
    if ((st = zx_pci_config_read(device_.handle, static_cast<uint16_t>(cap_offset), sizeof(uint8_t),
                                 &type_id)) != ZX_OK) {
      zxlogf(ERROR, "%s: error reading type from cap offset %#x: %d", __func__, cap_offset, st);
      return st;
    }

    if (type_id == cap_id) {
      *out_offset = static_cast<uint8_t>(cap_offset);
      return ZX_OK;
    }

    // We didn't find the right type, move on, but ensure we're still within the
    // first 256 bytes of standard config space.
    if (cap_offset >= UINT8_MAX) {
      zxlogf(ERROR, "%s: %#x is an invalid capability offset!", __func__, cap_offset);
      break;
    }
    if ((st = zx_pci_config_read(device_.handle, static_cast<uint16_t>(cap_offset + 1),
                                 sizeof(uint8_t), &cap_offset)) != ZX_OK) {
      zxlogf(ERROR, "%s: error reading next cap from cap offset %#x: %d", __func__, cap_offset + 1,
             st);
      break;
    }
  }
  return ZX_ERR_BAD_STATE;
}

zx_status_t KernelPci::PciGetFirstExtendedCapability(uint16_t cap_id, uint16_t* out_offset) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t KernelPci::PciGetNextExtendedCapability(uint16_t cap_id, uint16_t offset,
                                                    uint16_t* out_offset) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t KernelPci::PciGetBti(uint32_t index, zx::bti* out_bti) {
  zx_status_t st = ZX_ERR_NOT_SUPPORTED;
  uint32_t bdf = (static_cast<uint32_t>(device_.info.bus_id) << 8) |
                 (static_cast<uint32_t>(device_.info.dev_id) << 3) | device_.info.func_id;
  if (device_.pciroot.ops) {
    st = pciroot_get_bti(&device_.pciroot, bdf, index, out_bti->reset_and_get_address());
  } else if (device_.pdev.ops) {
    // TODO(teisenbe): This isn't quite right. We need to develop a way to
    // resolve which BTI should go to downstream. However, we don't currently
    // support any SMMUs for ARM, so this will work for now.
    st = pdev_get_bti(&device_.pdev, 0, out_bti->reset_and_get_address());
  }

  return st;
}

zx_status_t KernelPci::SysmemConnect(zx::channel allocator_request) {
  zx_status_t st = ZX_ERR_NOT_SUPPORTED;
  if (device_.pciroot.ops) {
    st = pciroot_connect_sysmem(&device_.pciroot, allocator_request.get());
    if (st == ZX_OK) {
      static_cast<void>(allocator_request.release());
    }
  }
  return st;
}

zx_status_t KernelPci::RpcReply(zx_handle_t ch, zx_status_t status, zx_handle_t* handle,
                                PciRpcMsg* req, PciRpcMsg* resp) {
  uint32_t handle_cnt = 0;
  if (handle && *handle != ZX_HANDLE_INVALID) {
    handle_cnt++;
  }

  resp->op = req->op;
  resp->txid = req->txid;
  resp->ret = status;
  return zx_channel_write(ch, 0, resp, sizeof(PciRpcMsg), handle, handle_cnt);
}

zx_status_t KernelPci::RpcEnableBusMaster(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  return RpcReply(ch, PciEnableBusMaster(req->enable), nullptr, req, resp);
}

zx_status_t KernelPci::RpcResetDevice(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  return RpcReply(ch, PciResetDevice(), nullptr, req, resp);
}

// Reads from a config space address for a given device handle. Most of the
// heavy lifting is offloaded to the zx_pci_config_read syscall itself, and the
// rpc client that formats the arguments.
zx_status_t KernelPci::RpcConfigRead(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  resp->cfg = req->cfg;
  zx_status_t st = ZX_ERR_INVALID_ARGS;
  switch (req->cfg.width) {
    case 1:
      st = PciConfigRead8(req->cfg.offset, reinterpret_cast<uint8_t*>(&resp->cfg.value));
      break;
    case 2:
      st = PciConfigRead16(req->cfg.offset, reinterpret_cast<uint16_t*>(&resp->cfg.value));
      break;
    case 4:
      st = PciConfigRead32(req->cfg.offset, &resp->cfg.value);
      break;
  }

  return RpcReply(ch, st, nullptr, req, resp);
}

zx_status_t KernelPci::RpcConfigWrite(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  zx_status_t st = ZX_ERR_INVALID_ARGS;
  switch (req->cfg.width) {
    case 1:
      st = PciConfigWrite8(req->cfg.offset, req->cfg.value);
      break;
    case 2:
      st = PciConfigWrite16(req->cfg.offset, req->cfg.value);
      break;
    case 4:
      st = PciConfigWrite32(req->cfg.offset, req->cfg.value);
      break;
  }

  return RpcReply(ch, st, nullptr, req, resp);
}

// Retrieves either address information for PIO or a VMO corresponding to a
// device's bar to pass back to the devhost making the call.
zx_status_t KernelPci::RpcGetBar(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  pci_bar_t bar{};
  zx_status_t st = PciGetBar(req->bar.id, &bar);
  if (st == ZX_OK) {
    resp->bar = {
        .id = bar.id,
        .is_mmio = (bar.type == ZX_PCI_BAR_TYPE_MMIO),
        .size = bar.size,
        .address = bar.address,
    };

    handle = bar.handle;
  }
  return RpcReply(ch, st, &handle, req, resp);
}

zx_status_t KernelPci::RpcQueryIrqMode(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  uint32_t max_irqs{};
  zx_status_t st = PciQueryIrqMode(req->irq.mode, &max_irqs);
  if (st == ZX_OK) {
    resp->irq.mode = req->irq.mode;
    resp->irq.max_irqs = max_irqs;
  }
  return RpcReply(ch, st, nullptr, req, resp);
}

zx_status_t KernelPci::RpcSetIrqMode(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  return RpcReply(ch, PciSetIrqMode(req->irq.mode, req->irq.requested_irqs), nullptr, req, resp);
}

zx_status_t KernelPci::RpcConfigureIrqMode(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  return RpcReply(ch, PciConfigureIrqMode(req->irq.requested_irqs, &resp->irq.mode), nullptr, req,
                  resp);
}

zx_status_t KernelPci::RpcGetNextCapability(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  auto* offset_cast = reinterpret_cast<uint8_t*>(&resp->cap.offset);
  zx_status_t st = (req->cap.is_first)
                       ? PciGetFirstCapability(req->cap.id, offset_cast)
                       : PciGetNextCapability(req->cap.id, req->cap.offset + 1, offset_cast);

  return RpcReply(ch, st, nullptr, req, resp);
}

zx_status_t KernelPci::RpcMapInterrupt(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  zx::interrupt interrupt;
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t st = PciMapInterrupt(req->irq.which_irq, &interrupt);
  if (st == ZX_OK) {
    handle = interrupt.release();
  }
  return RpcReply(ch, st, &handle, req, resp);
}

zx_status_t KernelPci::RpcAckInterrupt(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  return RpcReply(ch, PciAckInterrupt(), nullptr, req, resp);
}

zx_status_t KernelPci::RpcGetDeviceInfo(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  return RpcReply(ch, PciGetDeviceInfo(&resp->info), nullptr, req, resp);
}

zx_status_t KernelPci::RpcGetBti(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp) {
  zx::bti bti;
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t st = PciGetBti(req->bti_index, &bti);
  if (st == ZX_OK) {
    handle = bti.release();
  }

  return RpcReply(ch, st, &handle, req, resp);
}

zx_status_t KernelPci::RpcConnectSysmem(zx_handle_t ch, zx_handle_t handle, PciRpcMsg* req,
                                        PciRpcMsg* resp) {
  zx::channel allocator_request(handle);
  return RpcReply(ch, SysmemConnect(std::move(allocator_request)), nullptr, req, resp);
}

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

  kpci_device device{};
  device.info = info;
  device.handle = handle;
  device.index = index;

  // Store the PCIROOT protocol for use with get_bti in the pci protocol It is
  // not fatal if this fails, but bti protocol methods will not work.
  device_get_protocol(parent, ZX_PROTOCOL_PCIROOT, &device.pciroot);
  device_get_protocol(parent, ZX_PROTOCOL_PDEV, &device.pdev);

  snprintf(device.name, sizeof(device.name), "%02x:%02x.%1x", device.info.bus_id,
           device.info.dev_id, device.info.func_id);
  status = KernelPci::CreateSimple(parent, device);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to create kPCIProxy for %#02x:%#02x.%1x (%#04x:%#04x)", info.bus_id,
           info.dev_id, info.func_id, info.vendor_id, info.device_id);
  }

  status = KernelPci::CreateComposite(parent, device);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to create kPCI for %#02x:%#02x.%1x (%#04x:%#04x)", info.bus_id,
           info.dev_id, info.func_id, info.vendor_id, info.device_id);
    return status;
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
