// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <endian.h>
#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/pci/pciroot.h>
#include <lib/pci/pio.h>
#include <zircon/compiler.h>
#include <zircon/hw/i2c.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <array>
#include <memory>

#include <acpica/acpi.h>

#include "src/devices/board/lib/acpi/device.h"
#include "src/devices/board/lib/acpi/pci-internal.h"
#include "src/devices/lib/iommu/iommu.h"

static zx_status_t pciroot_op_get_bti(void* /*context*/, uint32_t bdf, uint32_t index,
                                      zx_handle_t* bti) {
  // The x86 IOMMU world uses PCI BDFs as the hardware identifiers, so there
  // will only be one BTI per device.
  if (index != 0) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  // For dummy IOMMUs, the bti_id just needs to be unique.  For Intel IOMMUs,
  // the bti_ids correspond to PCI BDFs.
  zx_handle_t iommu_handle;
  zx_status_t status = iommu_manager_iommu_for_bdf(bdf, &iommu_handle);
  if (status != ZX_OK) {
    return status;
  }
  return zx_bti_create(iommu_handle, 0, bdf, bti);
}

zx_status_t x64Pciroot::PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
  return pciroot_op_get_bti(nullptr, bdf, index, bti->reset_and_get_address());
}

zx_status_t x64Pciroot::PcirootGetPciPlatformInfo(pci_platform_info_t* info) {
  *info = context_.info;
  info->irq_routing_list = context_.routing.data();
  info->irq_routing_count = context_.routing.size();
  info->acpi_bdfs_list = acpi_bdfs_.data();
  info->acpi_bdfs_count = acpi_bdfs_.size();

  return ZX_OK;
}

zx_status_t x64Pciroot::PcirootReadConfig8(const pci_bdf_t* address, uint16_t offset,
                                           uint8_t* value) {
  return pci_pio_read8(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootReadConfig16(const pci_bdf_t* address, uint16_t offset,
                                            uint16_t* value) {
  return pci_pio_read16(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootReadConfig32(const pci_bdf_t* address, uint16_t offset,
                                            uint32_t* value) {
  return pci_pio_read32(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootWriteConfig8(const pci_bdf_t* address, uint16_t offset,
                                            uint8_t value) {
  return pci_pio_write8(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootWriteConfig16(const pci_bdf_t* address, uint16_t offset,
                                             uint16_t value) {
  return pci_pio_write16(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootWriteConfig32(const pci_bdf_t* address, uint16_t offset,
                                             uint32_t value) {
  return pci_pio_write32(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::Create(PciRootHost* root_host, x64Pciroot::Context ctx, zx_device_t* parent,
                               const char* name, std::vector<pci_bdf_t> acpi_bdfs) {
  auto pciroot = new x64Pciroot(root_host, std::move(ctx), parent, name, std::move(acpi_bdfs));
  return pciroot->DdkAdd(name);
}
