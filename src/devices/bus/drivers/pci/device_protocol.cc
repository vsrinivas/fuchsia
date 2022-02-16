// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include <fuchsia/hardware/pci/c/banjo.h>
#include <lib/ddk/debug.h>
#include <zircon/syscalls/pci.h>
#include <zircon/types.h>

#include "src/devices/bus/drivers/pci/common.h"
#include "src/devices/bus/drivers/pci/device.h"
#include "src/devices/bus/drivers/pci/proxy_rpc.h"

#define LOG_STATUS(level, status, format, ...)                                                   \
  ({                                                                                             \
    zx_status_t _status = (status);                                                              \
    zxlogf(level, "[%s] %s(" format ") = %s", cfg_->addr(), __func__ __VA_OPT__(, ) __VA_ARGS__, \
           zx_status_get_string(_status));                                                       \
    _status;                                                                                     \
  })

namespace pci {

zx_status_t Device::DdkGetProtocol(uint32_t proto_id, void* out) {
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

template <typename V, typename R>
zx_status_t Device::ConfigRead(uint16_t offset, V* value) {
  if (offset >= PCI_EXT_CONFIG_SIZE) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *value = cfg_->Read(R(offset));
  return ZX_OK;
}

zx_status_t Device::PciConfigRead8(uint16_t offset, uint8_t* out_value) {
  zx_status_t status = ConfigRead<uint8_t, PciReg8>(offset, out_value);
  return LOG_STATUS(TRACE, status, "%#x", offset);
}

zx_status_t Device::PciConfigRead16(uint16_t offset, uint16_t* out_value) {
  zx_status_t status = ConfigRead<uint16_t, PciReg16>(offset, out_value);
  return LOG_STATUS(TRACE, status, "%#x", offset);
}

zx_status_t Device::PciConfigRead32(uint16_t offset, uint32_t* out_value) {
  zx_status_t status = ConfigRead<uint32_t, PciReg32>(offset, out_value);
  return LOG_STATUS(TRACE, status, "%#x", offset);
}

template <typename V, typename R>
zx_status_t Device::ConfigWrite(uint16_t offset, V value) {
  // Don't permit writes inside the config header.
  if (offset < PCI_CONFIG_HDR_SIZE) {
    return ZX_ERR_ACCESS_DENIED;
  }

  if (offset >= PCI_EXT_CONFIG_SIZE) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  cfg_->Write(R(offset), value);
  return ZX_OK;
}

zx_status_t Device::PciConfigWrite8(uint16_t offset, uint8_t value) {
  zx_status_t status = ConfigWrite<uint8_t, PciReg8>(offset, value);
  return LOG_STATUS(TRACE, status, "%#x, %#x", offset, value);
}

zx_status_t Device::PciConfigWrite16(uint16_t offset, uint16_t value) {
  zx_status_t status = ConfigWrite<uint16_t, PciReg16>(offset, value);
  return LOG_STATUS(TRACE, status, "%#x, %#x", offset, value);
}

zx_status_t Device::PciConfigWrite32(uint16_t offset, uint32_t value) {
  zx_status_t status = ConfigWrite<uint32_t, PciReg32>(offset, value);
  return LOG_STATUS(TRACE, status, "%#x, %#x", offset, value);
}

zx_status_t Device::PciEnableBusMaster(bool enable) {
  fbl::AutoLock dev_lock(&dev_lock_);
  zx_status_t status = EnableBusMaster(enable);
  return LOG_STATUS(DEBUG, status, "%d", enable);
}

zx_status_t Device::PciGetBar(uint32_t bar_id, pci_bar_t* out_bar) {
  zx_status_t status = ZX_OK;
  fbl::AutoLock dev_lock(&dev_lock_);
  if (bar_id >= bar_count_) {
    return LOG_STATUS(DEBUG, ZX_ERR_INVALID_ARGS, "%u", bar_id);
  }

  // Both unused BARs and BARs that are the second half of a 64 bit
  // BAR have a size of zero.
  auto& bar = bars_[bar_id];
  if (bar.size == 0) {
    return LOG_STATUS(DEBUG, ZX_ERR_NOT_FOUND, "%u", bar_id);
  }

  size_t bar_size = bar.size;
#ifdef ENABLE_MSIX
  // If this device shares BAR data with either of the MSI-X tables then we need
  // to determine what portions of the BAR the driver can be permitted to
  // access.
  if (caps_.msix) {
    zx::status<size_t> result = caps_.msix->GetBarDataSize(bar);
    if (!result.is_ok()) {
      return LOG_STATUS(DEBUG, result.status_value(), "%u", bar_id);
    }
    bar_size = result.value();
  }
#endif

  out_bar->id = bar_id;
  out_bar->address = bar.address;
  out_bar->size = bar_size;
  out_bar->type = (bar.is_mmio) ? ZX_PCI_BAR_TYPE_MMIO : ZX_PCI_BAR_TYPE_PIO;

  // MMIO Bars have an associated VMO for the driver to map, whereas IO bars
  // have a Resource corresponding to an IO range for the driver to access.
  // These are mutually exclusive, so only one handle is ever needed.
  status = ZX_ERR_INTERNAL;
  if (bar.is_mmio) {
    zx::vmo vmo = {};
    if ((status = bar.allocation->CreateVmObject(&vmo)) == ZX_OK) {
      out_bar->handle = vmo.release();
    }
  } else {  // Bar using IOports
    zx::resource res = {};
    if (bar.allocation->resource() &&
        (status = bar.allocation->resource().duplicate(ZX_RIGHT_SAME_RIGHTS, &res)) == ZX_OK) {
      out_bar->handle = res.release();
    }
  }

  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Failed to create %s for BAR %u (type = %s, range = [%#lx, %#lx)): %s",
           cfg_->addr(), (bar.is_mmio) ? "VMO" : "resource", bar_id, (bar.is_mmio) ? "MMIO" : "IO",
           bar.address, bar.address + bar.size, zx_status_get_string(status));
  }

  return LOG_STATUS(DEBUG, status, "%u", bar_id);
}

zx_status_t Device::PciGetBti(uint32_t index, zx::bti* out_bti) {
  fbl::AutoLock dev_lock(&dev_lock_);
  zx_status_t status = bdi_->GetBti(this, index, out_bti);
  return LOG_STATUS(DEBUG, status, "%u", index);
}

zx_status_t Device::PciGetDeviceInfo(pcie_device_info_t* out_info) {
  out_info->vendor_id = vendor_id();
  out_info->device_id = device_id();
  out_info->base_class = class_id();
  out_info->sub_class = subclass();
  out_info->program_interface = prog_if();
  out_info->revision_id = rev_id();
  out_info->bus_id = bus_id();
  out_info->dev_id = dev_id();
  out_info->func_id = func_id();
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

zx_status_t Device::PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset) {
  zx_status_t status = GetFirstOrNextCapability<uint8_t, CapabilityList>(
      capabilities().list, cap_id, /*is_first=*/true, std::nullopt, out_offset);
  return LOG_STATUS(DEBUG, status, "%#x", cap_id);
}

zx_status_t Device::PciGetNextCapability(uint8_t cap_id, uint8_t offset, uint8_t* out_offset) {
  zx_status_t status =
      GetFirstOrNextCapability<uint8_t, CapabilityList>(capabilities().list, cap_id,
                                                        /*is_first=*/false, offset, out_offset);
  return LOG_STATUS(DEBUG, status, "%#x, %#x", cap_id, offset);
}

zx_status_t Device::PciGetFirstExtendedCapability(uint16_t cap_id, uint16_t* out_offset) {
  zx_status_t status = GetFirstOrNextCapability<uint16_t, ExtCapabilityList>(
      capabilities().ext_list, cap_id, true, std::nullopt, out_offset);
  return LOG_STATUS(DEBUG, status, "%#x", cap_id);
}

zx_status_t Device::PciGetNextExtendedCapability(uint16_t cap_id, uint16_t offset,
                                                 uint16_t* out_offset) {
  zx_status_t status = GetFirstOrNextCapability<uint16_t, ExtCapabilityList>(
      capabilities().ext_list, cap_id, false, offset, out_offset);
  return LOG_STATUS(DEBUG, status, "%#x, %#x", cap_id, offset);
}

zx_status_t Device::PciQueryIrqMode(pci_irq_mode_t mode, uint32_t* out_max_irqs) {
  auto result = QueryIrqMode(mode);
  if (result.is_ok()) {
    *out_max_irqs = result.value();
  }

  return LOG_STATUS(DEBUG, result.status_value(), "%u", mode);
}

void Device::PciGetInterruptModes(pci_interrupt_modes_t* modes) { *modes = GetInterruptModes(); }

zx_status_t Device::PciSetInterruptMode(pci_irq_mode_t mode, uint32_t requested_irq_count) {
  zx_status_t status = SetIrqMode(mode, requested_irq_count);
  return LOG_STATUS(DEBUG, status, "%u, %u", mode, requested_irq_count);
}

zx_status_t Device::PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle) {
  zx::status<zx::interrupt> result = MapInterrupt(which_irq);
  if (result.is_ok()) {
    *out_handle = std::move(result.value());
  }

  return LOG_STATUS(DEBUG, result.status_value(), "%u", which_irq);
}

zx_status_t Device::PciAckInterrupt() {
  fbl::AutoLock dev_lock(&dev_lock_);
  return AckLegacyIrq();
}

zx_status_t Device::PciResetDevice() { return LOG_STATUS(DEBUG, ZX_ERR_NOT_SUPPORTED, ""); }

}  // namespace pci
