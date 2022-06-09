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

zx_status_t AcpiPciroot::PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
  // x86 uses PCI BDFs as hardware identifiers, and ARM uses PCI root complexes. There will be at
  // most one BTI per device.
  if (index != 0) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto iommu = context_.iommu->IommuForPciDevice(bdf);
  return zx::bti::create(*iommu, 0, bdf, bti);
}

zx_status_t AcpiPciroot::PcirootGetPciPlatformInfo(pci_platform_info_t* info) {
  *info = context_.info;
  info->irq_routing_list = context_.routing.data();
  info->irq_routing_count = context_.routing.size();
  info->acpi_bdfs_list = acpi_bdfs_.data();
  info->acpi_bdfs_count = acpi_bdfs_.size();

  return ZX_OK;
}

zx_status_t AcpiPciroot::PcirootReadConfig8(const pci_bdf_t* address, uint16_t offset,
                                            uint8_t* value) {
  return pci_pio_read8(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t AcpiPciroot::PcirootReadConfig16(const pci_bdf_t* address, uint16_t offset,
                                             uint16_t* value) {
  return pci_pio_read16(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t AcpiPciroot::PcirootReadConfig32(const pci_bdf_t* address, uint16_t offset,
                                             uint32_t* value) {
  return pci_pio_read32(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t AcpiPciroot::PcirootWriteConfig8(const pci_bdf_t* address, uint16_t offset,
                                             uint8_t value) {
  return pci_pio_write8(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t AcpiPciroot::PcirootWriteConfig16(const pci_bdf_t* address, uint16_t offset,
                                              uint16_t value) {
  return pci_pio_write16(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t AcpiPciroot::PcirootWriteConfig32(const pci_bdf_t* address, uint16_t offset,
                                              uint32_t value) {
  return pci_pio_write32(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t AcpiPciroot::Create(PciRootHost* root_host, AcpiPciroot::Context ctx,
                                zx_device_t* parent, const char* name,
                                std::vector<pci_bdf_t> acpi_bdfs) {
  auto pciroot = new AcpiPciroot(root_host, std::move(ctx), parent, name, std::move(acpi_bdfs));
  return pciroot->DdkAdd(name);
}
