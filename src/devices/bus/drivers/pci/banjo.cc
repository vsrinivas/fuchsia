// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
// TODO(fxbug.dev/33713): Stop depending on the types in this file.
#include <zircon/syscalls/pci.h>
#include <zircon/types.h>

#include <bind/fuchsia/acpi/cpp/fidl.h>
#include <bind/fuchsia/pci/cpp/fidl.h>
#include <ddktl/device.h>
#include <fbl/alloc_checker.h>

#include "src/devices/bus/drivers/pci/common.h"
#include "src/devices/bus/drivers/pci/device.h"
#include "src/devices/bus/drivers/pci/proxy_rpc.h"

#define LOG_STATUS(level, status, format, ...)                                  \
  ({                                                                            \
    zx_status_t _status = (status);                                             \
    zxlogf(level, "[%s] %s(" format ") = %s", device()->config()->addr(),       \
           __func__ __VA_OPT__(, ) __VA_ARGS__, zx_status_get_string(_status)); \
    _status;                                                                    \
  })

namespace pci {

zx::status<> BanjoDevice::Create(zx_device_t* parent, pci::Device* device) {
  fbl::AllocChecker ac;
  std::unique_ptr<BanjoDevice> banjo_dev(new (&ac) BanjoDevice(parent, std::move(device)));
  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }
  auto pci_dev = banjo_dev->device();

  auto pci_bind_topo = static_cast<uint32_t>(
      BIND_PCI_TOPO_PACK(pci_dev->bus_id(), pci_dev->dev_id(), pci_dev->func_id()));
  // clang-format off
  zx_device_prop_t pci_device_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, pci_dev->vendor_id()},
      {BIND_PCI_DID, 0, pci_dev->device_id()},
      {BIND_PCI_CLASS, 0, pci_dev->class_id()},
      {BIND_PCI_SUBCLASS, 0, pci_dev->subclass()},
      {BIND_PCI_INTERFACE, 0, pci_dev->prog_if()},
      {BIND_PCI_REVISION, 0, pci_dev->rev_id()},
      {BIND_PCI_TOPO, 0, pci_bind_topo},
  };
  // clang-format on

  // Create an isolated devhost to load the proxy pci driver containing the PciProxy
  // instance which will talk to this device.
  zx_status_t status = banjo_dev->DdkAdd(ddk::DeviceAddArgs(pci_dev->config()->addr())
                                             .set_props(pci_device_props)
                                             .set_proto_id(ZX_PROTOCOL_PCI)
                                             .set_flags(DEVICE_ADD_MUST_ISOLATE));
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Failed to create pci banjo fragment: %s", pci_dev->config()->addr(),
           zx_status_get_string(status));
    return zx::error(status);
  }

  auto banjo_dev_unowned = banjo_dev.release();
  // TODO(fxbug.dev/93333): Remove this once DFv2 is stabilised.
  bool is_dfv2 = device_is_dfv2(banjo_dev_unowned->zxdev());
  if (is_dfv2) {
    return zx::ok();
  }

  const zx_bind_inst_t pci_fragment_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
      BI_ABORT_IF(NE, BIND_PCI_VID, pci_dev->vendor_id()),
      BI_ABORT_IF(NE, BIND_PCI_DID, pci_dev->device_id()),
      BI_ABORT_IF(NE, BIND_PCI_CLASS, pci_dev->class_id()),
      BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, pci_dev->subclass()),
      BI_ABORT_IF(NE, BIND_PCI_INTERFACE, pci_dev->prog_if()),
      BI_ABORT_IF(NE, BIND_PCI_REVISION, pci_dev->rev_id()),
      BI_ABORT_IF(EQ, BIND_COMPOSITE, 1),
      BI_MATCH_IF(EQ, BIND_PCI_TOPO, pci_bind_topo),
  };

  const device_fragment_part_t pci_fragment[] = {
      {std::size(pci_fragment_match), pci_fragment_match},
  };

  const zx_bind_inst_t sysmem_match[] = {
      BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
  };

  const device_fragment_part_t sysmem_fragment[] = {
      {std::size(sysmem_match), sysmem_match},
  };

  const zx_bind_inst_t acpi_fragment_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_ACPI),
      BI_ABORT_IF(NE, BIND_ACPI_BUS_TYPE, bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_PCI),
      BI_MATCH_IF(EQ, BIND_PCI_TOPO, pci_bind_topo),
  };

  const device_fragment_part_t acpi_fragment[] = {
      {std::size(acpi_fragment_match), acpi_fragment_match},
  };

  // These are laid out so that ACPI can be optionally included via the number
  // of fragments specified.
  const device_fragment_t fragments[] = {
      {"pci", std::size(pci_fragment), pci_fragment},
      {"sysmem", std::size(sysmem_fragment), sysmem_fragment},
      {"acpi", std::size(acpi_fragment), acpi_fragment},
  };

  composite_device_desc_t composite_desc = {
      .props = pci_device_props,
      .props_count = std::size(pci_device_props),
      .fragments = fragments,
      .fragments_count = (pci_dev->has_acpi()) ? std::size(fragments) : std::size(fragments) - 1,
      .primary_fragment = "pci",
      .spawn_colocated = false,
  };

  char composite_name[ZX_DEVICE_NAME_MAX];
  snprintf(composite_name, sizeof(composite_name), "pci-%s", pci_dev->config()->addr());
  status = banjo_dev_unowned->DdkAddComposite(composite_name, &composite_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Failed to create pci banjo composite: %s", pci_dev->config()->addr(),
           zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::ok();
}

zx_status_t BanjoDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
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

zx_status_t BanjoDevice::PciReadConfig8(uint16_t offset, uint8_t* out_value) {
  zx::status<uint8_t> result = device_->ReadConfig<uint8_t, PciReg8>(offset);
  if (result.is_ok()) {
    *out_value = result.value();
  }
  return LOG_STATUS(TRACE, result.status_value(), "%#x", offset);
}

zx_status_t BanjoDevice::PciReadConfig16(uint16_t offset, uint16_t* out_value) {
  zx::status<uint16_t> result = device_->ReadConfig<uint16_t, PciReg16>(offset);
  if (result.is_ok()) {
    *out_value = result.value();
  }
  return LOG_STATUS(TRACE, result.status_value(), "%#x", offset);
}

zx_status_t BanjoDevice::PciReadConfig32(uint16_t offset, uint32_t* out_value) {
  zx::status<uint32_t> result = device_->ReadConfig<uint32_t, PciReg32>(offset);
  if (result.is_ok()) {
    *out_value = result.value();
  }
  return LOG_STATUS(TRACE, result.status_value(), "%#x", offset);
}

zx_status_t BanjoDevice::PciWriteConfig8(uint16_t offset, uint8_t value) {
  zx_status_t status = device_->WriteConfig<uint8_t, PciReg8>(offset, value);
  return LOG_STATUS(TRACE, status, "%#x, %#x", offset, value);
}

zx_status_t BanjoDevice::PciWriteConfig16(uint16_t offset, uint16_t value) {
  zx_status_t status = device_->WriteConfig<uint16_t, PciReg16>(offset, value);
  return LOG_STATUS(TRACE, status, "%#x, %#x", offset, value);
}

zx_status_t BanjoDevice::PciWriteConfig32(uint16_t offset, uint32_t value) {
  zx_status_t status = device_->WriteConfig<uint32_t, PciReg32>(offset, value);
  return LOG_STATUS(TRACE, status, "%#x, %#x", offset, value);
}

zx_status_t BanjoDevice::PciSetBusMastering(bool enable) {
  fbl::AutoLock dev_lock(device_->dev_lock());
  zx_status_t status = device_->SetBusMastering(enable);
  return LOG_STATUS(DEBUG, status, "%d", enable);
}

zx_status_t BanjoDevice::PciGetBar(uint32_t bar_id, pci_bar_t* out_bar) {
  zx_status_t status = ZX_OK;
  fbl::AutoLock dev_lock(device_->dev_lock());
  if (bar_id >= device_->bar_count()) {
    return LOG_STATUS(DEBUG, ZX_ERR_INVALID_ARGS, "%u", bar_id);
  }

  // Don't return bars corresponding to unused bars or the upper half
  // of a 64 bit bar.
  auto& bar = device_->bars()[bar_id];
  if (!bar) {
    return LOG_STATUS(DEBUG, ZX_ERR_NOT_FOUND, "%u", bar_id);
  }

  size_t bar_size = bar->size;
#ifdef ENABLE_MSIX
  // If this device shares BAR data with either of the MSI-X tables then we need
  // to determine what portions of the BAR the driver can be permitted to
  // access.
  if (device_->capabilities().msix) {
    zx::status<size_t> result = device_->capabilities().msix->GetBarDataSize(*bar);
    if (result.is_error()) {
      return LOG_STATUS(DEBUG, result.status_value(), "%u", bar_id);
    }
    bar_size = result.value();
  }
#endif

  out_bar->bar_id = bar_id;
  out_bar->size = bar_size;
  out_bar->type = (bar->is_mmio) ? PCI_BAR_TYPE_MMIO : PCI_BAR_TYPE_IO;

  // MMIO Bars have an associated VMO for the driver to map, whereas IO bars
  // have a Resource corresponding to an IO range for the driver to access.
  // These are mutually exclusive, so only one handle is ever needed.
  zx::status<zx::handle> result;
  if (bar->is_mmio) {
    result = bar->allocation->CreateVmo();
    if (result.is_ok()) {
      out_bar->result.vmo = result.value().release();
    }
  } else {  // Bar using IOports
    result = bar->allocation->CreateResource();
    if (result.is_ok()) {
      out_bar->result.io.resource = result.value().release();
      out_bar->result.io.address = bar->address;
    }
  }

  if (result.is_error()) {
    zxlogf(ERROR, "[%s] Failed to create %s for BAR %u (type = %s, range = [%#lx, %#lx)): %s",
           device_->config()->addr(), (bar->is_mmio) ? "VMO" : "resource", bar_id,
           (bar->is_mmio) ? "MMIO" : "IO", bar->address, bar->address + bar->size,
           zx_status_get_string(status));
  }
  return LOG_STATUS(DEBUG, result.status_value(), "%u", bar_id);
}

zx_status_t BanjoDevice::PciGetBti(uint32_t index, zx::bti* out_bti) {
  fbl::AutoLock dev_lock(device_->dev_lock());
  zx_status_t status = device_->bdi()->GetBti(device_, index, out_bti);
  return LOG_STATUS(DEBUG, status, "%u", index);
}

zx_status_t BanjoDevice::PciGetDeviceInfo(pci_device_info_t* out_info) {
  out_info->vendor_id = device_->vendor_id();
  out_info->device_id = device_->device_id();
  out_info->base_class = device_->class_id();
  out_info->sub_class = device_->subclass();
  out_info->program_interface = device_->prog_if();
  out_info->revision_id = device_->rev_id();
  out_info->bus_id = device_->bus_id();
  out_info->dev_id = device_->dev_id();
  out_info->func_id = device_->func_id();
  return LOG_STATUS(DEBUG, ZX_OK, "");
}

namespace {

// Capabilities and Extended Capabilities only differ by what list they're in along with the
// size of their entries. We can offload most of the work into a templated work function.
template <class T, class L>
zx_status_t GetFirstOrNextCapability(const L& list, T cap_id, bool is_first,
                                     std::optional<T> scan_offset, T* out_offset) {
  // Scan for the capability type requested, returning the first capability
  // found after we've seen the capability owning the previous offset.  We
  // can't scan entirely based on offset being >= than a given base because
  // capabilities pointers can point backwards in config space as long as the
  // structures are valid.
  bool found_prev = is_first;

  for (auto& cap : list) {
    if (found_prev) {
      if (cap.id() == cap_id) {
        *out_offset = cap.base();
        return ZX_OK;
      }
    } else {
      if (cap.base() == scan_offset) {
        found_prev = true;
      }
    }
  }
  return ZX_ERR_NOT_FOUND;
}

}  // namespace

zx_status_t BanjoDevice::PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset) {
  zx_status_t status = GetFirstOrNextCapability<uint8_t, CapabilityList>(
      device_->capabilities().list, cap_id, /*is_first=*/true, std::nullopt, out_offset);
  return LOG_STATUS(DEBUG, status, "%#x", cap_id);
}

zx_status_t BanjoDevice::PciGetNextCapability(uint8_t cap_id, uint8_t offset, uint8_t* out_offset) {
  zx_status_t status =
      GetFirstOrNextCapability<uint8_t, CapabilityList>(device_->capabilities().list, cap_id,
                                                        /*is_first=*/false, offset, out_offset);
  return LOG_STATUS(DEBUG, status, "%#x, %#x", cap_id, offset);
}

zx_status_t BanjoDevice::PciGetFirstExtendedCapability(uint16_t cap_id, uint16_t* out_offset) {
  zx_status_t status = GetFirstOrNextCapability<uint16_t, ExtCapabilityList>(
      device_->capabilities().ext_list, cap_id, true, std::nullopt, out_offset);
  return LOG_STATUS(DEBUG, status, "%#x", cap_id);
}

zx_status_t BanjoDevice::PciGetNextExtendedCapability(uint16_t cap_id, uint16_t offset,
                                                      uint16_t* out_offset) {
  zx_status_t status = GetFirstOrNextCapability<uint16_t, ExtCapabilityList>(
      device_->capabilities().ext_list, cap_id, false, offset, out_offset);
  return LOG_STATUS(DEBUG, status, "%#x, %#x", cap_id, offset);
}

void BanjoDevice::PciGetInterruptModes(pci_interrupt_modes_t* modes) {
  *modes = device_->GetInterruptModes();
}

zx_status_t BanjoDevice::PciSetInterruptMode(pci_interrupt_mode_t mode,
                                             uint32_t requested_irq_count) {
  zx_status_t status = device_->SetIrqMode(mode, requested_irq_count);
  return LOG_STATUS(DEBUG, status, "%u, %u", mode, requested_irq_count);
}

zx_status_t BanjoDevice::PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle) {
  zx::status<zx::interrupt> result = device_->MapInterrupt(which_irq);
  if (result.is_ok()) {
    *out_handle = std::move(result.value());
  }

  return LOG_STATUS(DEBUG, result.status_value(), "%u", which_irq);
}

zx_status_t BanjoDevice::PciAckInterrupt() {
  fbl::AutoLock dev_lock(device_->dev_lock());
  return device_->AckLegacyIrq();
}

zx_status_t BanjoDevice::PciResetDevice() { return LOG_STATUS(DEBUG, ZX_ERR_NOT_SUPPORTED, ""); }

}  // namespace pci
