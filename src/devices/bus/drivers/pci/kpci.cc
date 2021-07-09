// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/drivers/pci/kpci.h"

#include <fuchsia/hardware/pci/c/banjo.h>
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

#include <bind/fuchsia/acpi/cpp/fidl.h>
#include <bind/fuchsia/pci/cpp/fidl.h>
#include <ddktl/device.h>

#include "src/devices/bus/drivers/pci/pci_bind.h"
#include "src/devices/bus/drivers/pci/proxy_rpc.h"

namespace pci {

static const zx_bind_inst_t sysmem_fragment_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};

static const device_fragment_part_t sysmem_fragment[] = {
    {countof(sysmem_fragment_match), sysmem_fragment_match},
};

zx_status_t KernelPci::CreateComposite(zx_device_t* parent, kpci_device device, bool uses_acpi) {
  auto pci_bind_topo = static_cast<uint32_t>(
      BIND_PCI_TOPO_PACK(device.info.bus_id, device.info.dev_id, device.info.func_id));
  zx_device_prop_t fragment_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, device.info.vendor_id},
      {BIND_PCI_DID, 0, device.info.device_id},
      {BIND_PCI_CLASS, 0, device.info.base_class},
      {BIND_PCI_SUBCLASS, 0, device.info.sub_class},
      {BIND_PCI_INTERFACE, 0, device.info.program_interface},
      {BIND_PCI_REVISION, 0, device.info.revision_id},
      {BIND_PCI_TOPO, 0, pci_bind_topo},
  };

  auto kpci = std::unique_ptr<KernelPci>(new KernelPci(parent, device));
  zx_status_t status = kpci->DdkAdd(
      ddk::DeviceAddArgs(device.name).set_props(fragment_props).set_proto_id(ZX_PROTOCOL_PCI));
  if (status != ZX_OK) {
    return status;
  }

  const zx_bind_inst_t pci_fragment_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
      BI_ABORT_IF(NE, BIND_PCI_VID, device.info.vendor_id),
      BI_ABORT_IF(NE, BIND_PCI_DID, device.info.device_id),
      BI_ABORT_IF(NE, BIND_PCI_CLASS, device.info.base_class),
      BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, device.info.sub_class),
      BI_ABORT_IF(NE, BIND_PCI_INTERFACE, device.info.program_interface),
      BI_ABORT_IF(NE, BIND_PCI_REVISION, device.info.revision_id),
      BI_ABORT_IF(EQ, BIND_COMPOSITE, 1),
      BI_MATCH_IF(EQ, BIND_PCI_TOPO, pci_bind_topo),
  };

  const device_fragment_part_t pci_fragment[] = {
      {countof(pci_fragment_match), pci_fragment_match},
  };

  const zx_bind_inst_t acpi_fragment_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_ACPI),
      BI_ABORT_IF(NE, BIND_ACPI_BUS_TYPE, bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_PCI),
      BI_MATCH_IF(EQ, BIND_PCI_TOPO, pci_bind_topo),
  };

  const device_fragment_part_t acpi_fragment[] = {
      {countof(acpi_fragment_match), acpi_fragment_match},
  };

  const device_fragment_t fragments[] = {
      {"sysmem", countof(sysmem_fragment), sysmem_fragment},
      {"pci", countof(pci_fragment), pci_fragment},
      {"acpi", countof(acpi_fragment), acpi_fragment},
  };
  zx_device_prop_t composite_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, device.info.vendor_id},
      {BIND_PCI_DID, 0, device.info.device_id},
      {BIND_PCI_CLASS, 0, device.info.base_class},
      {BIND_PCI_SUBCLASS, 0, device.info.sub_class},
      {BIND_PCI_INTERFACE, 0, device.info.program_interface},
      {BIND_PCI_REVISION, 0, device.info.revision_id},
      {BIND_PCI_TOPO, 0, pci_bind_topo},
  };

  composite_device_desc_t composite_desc = {
      .props = composite_props,
      .props_count = countof(composite_props),
      .fragments = fragments,
      .fragments_count = uses_acpi ? countof(fragments) : countof(fragments) - 1,
      .primary_fragment = "pci",
      .spawn_colocated = false,
  };

  char composite_name[ZX_DEVICE_NAME_MAX];
  snprintf(composite_name, sizeof(composite_name), "pci-%s", device.name);
  auto kpci_composite = std::unique_ptr<KernelPci>(new KernelPci(parent, device));
  status = kpci_composite->DdkAddComposite(composite_name, &composite_desc);
  if (status != ZX_OK) {
    return status;
  }

  static_cast<void>(kpci_composite.release());
  static_cast<void>(kpci.release());
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
  }

  return ZX_ERR_NOT_SUPPORTED;
}

void KernelPci::DdkRelease() {
  if (device_.handle != ZX_HANDLE_INVALID) {
    zx_handle_close(device_.handle);
  }
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
  // If we're looking for the first capability then we read from the offset
  // since it contains 0x34 which ppints to the start of the list. Otherwise, we
  // have an existing capability's offset and need to advance one byte to its
  // next pointer.
  if (offset != PCI_CFG_CAPABILITIES_PTR) {
    offset++;
  }

  // Walk the capability list looking for the type requested.  limit acts as a
  // barrier in case of an invalid capability pointer list that causes us to
  // iterate forever otherwise.
  uint8_t limit = 64;
  uint32_t cap_offset = 0;
  zx_pci_config_read(device_.handle, offset, sizeof(uint8_t), &cap_offset);
  while (cap_offset != 0 && cap_offset != 0xFF && limit--) {
    zx_status_t st;
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

  return ZX_ERR_NOT_FOUND;
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

// Initializes the upper half of a pci / pci.proxy devhost pair.
static zx_status_t pci_init_child(zx_device_t* parent, uint32_t index,
                                  pci_platform_info_t* plat_info) {
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

  bool uses_acpi = false;
  for (size_t i = 0; i < plat_info->acpi_bdfs_count; i++) {
    const pci_bdf_t* bdf = &plat_info->acpi_bdfs_list[i];
    if (bdf->bus_id == device.info.bus_id && bdf->device_id == device.info.dev_id &&
        bdf->function_id == device.info.func_id) {
      uses_acpi = true;
      break;
    }
  }

  snprintf(device.name, sizeof(device.name), "%02x:%02x.%1x", device.info.bus_id,
           device.info.dev_id, device.info.func_id);
  status = KernelPci::CreateComposite(parent, device, uses_acpi);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to create kPCI for %#02x:%#02x.%1x (%#04x:%#04x)", info.bus_id,
           info.dev_id, info.func_id, info.vendor_id, info.device_id);
    return status;
  }

  return status;
}  // namespace pci

static zx_status_t pci_drv_bind(void* ctx, zx_device_t* parent) {
  pci_platform_info_t platform_info{};
  pciroot_protocol_t pciroot;
  zx_status_t result = device_get_protocol(parent, ZX_PROTOCOL_PCIROOT, &pciroot);
  if (result == ZX_OK) {
    result = pciroot_get_pci_platform_info(&pciroot, &platform_info);
  }
  // Walk PCI devices to create their upper half devices until we hit the end
  for (uint32_t index = 0;; index++) {
    if (pci_init_child(parent, index, &platform_info) != ZX_OK) {
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
