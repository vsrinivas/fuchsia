// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/syscalls/types.h>

#include <array>
#include <limits>

#include <fbl/alloc_checker.h>

#include "qemu-bus.h"
#include "qemu-pciroot.h"
#include "qemu-virt.h"
namespace board_qemu_arm64 {

zx_status_t QemuArm64Pciroot::Create(PciRootHost* root_host, QemuArm64Pciroot::Context ctx,
                                     zx_device_t* parent, const char* name) {
  auto pciroot = new QemuArm64Pciroot(root_host, std::move(ctx), parent, name);
  return pciroot->DdkAdd(name);
}

zx_status_t QemuArm64Pciroot::PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t QemuArm64Pciroot::PcirootGetPciPlatformInfo(pci_platform_info_t* info) {
  *info = context_.info;
  return ZX_OK;
}

zx_status_t QemuArm64::PciInit() {
  zx_status_t status = pci_root_host_.Mmio32().AddRegion(
      {.base = PCIE_MMIO_BASE_PHYS, .size = PCIE_MMIO_SIZE}, RegionAllocator::AllowOverlap::No);

  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to add MMIO region { %#lx - %#lx } to PCI root allocator: %s",
           PCIE_MMIO_BASE_PHYS, PCIE_MMIO_BASE_PHYS + PCIE_MMIO_SIZE, zx_status_get_string(status));
    return status;
  }

  status = pci_root_host_.Mmio64().AddRegion(
      {.base = PCIE_MMIO_HIGH_BASE_PHYS, .size = PCIE_MMIO_HIGH_SIZE},
      RegionAllocator::AllowOverlap::No);

  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to add MMIO region { %#lx - %#lx } to PCI root allocator: %s",
           PCIE_MMIO_HIGH_BASE_PHYS, PCIE_MMIO_HIGH_BASE_PHYS + PCIE_MMIO_HIGH_SIZE,
           zx_status_get_string(status));
    return status;
  }

  if ((status = pci_root_host_.Io().AddRegion({.base = PCIE_PIO_BASE_PHYS, .size = PCIE_PIO_SIZE},
                                              RegionAllocator::AllowOverlap::No)) != ZX_OK) {
    zxlogf(ERROR, "Failed to add IO region { %#lx - %#lx } to the PCI root allocator: %s",
           PCIE_PIO_BASE_PHYS, PCIE_PIO_BASE_PHYS + PCIE_PIO_SIZE, zx_status_get_string(status));
    return status;
  }

  McfgAllocation pci0_mcfg = {
      .address = PCIE_ECAM_BASE_PHYS,
      .pci_segment = 0,
      .start_bus_number = 0,
      .end_bus_number = (PCIE_ECAM_SIZE / ZX_PCI_ECAM_BYTE_PER_BUS) - 1,
  };

  pci_root_host_.mcfgs().push_back(pci0_mcfg);
  return ZX_OK;
}

zx_status_t QemuArm64::PciAdd() {
  McfgAllocation pci0_mcfg = {};
  zx_status_t status = pci_root_host_.GetSegmentMcfgAllocation(0, &pci0_mcfg);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't retrieve the MMCFG for segment group %u: %s", 0,
           zx_status_get_string(status));
    return status;
  }

  // There's no dynamic configuration for this platform, so just grabbing the same mcfg
  // created in Init is adequate.
  std::array<char, 8> name = {"pci0"};
  QemuArm64Pciroot::Context ctx = {};
  ctx.info.start_bus_num = pci0_mcfg.start_bus_number;
  ctx.info.end_bus_num = pci0_mcfg.end_bus_number;
  ctx.info.segment_group = pci0_mcfg.pci_segment;
  memcpy(ctx.info.name, name.data(), name.size());

  zxlogf(DEBUG, "%s ecam { %#lx - %#lx }\n", name.data(), PCIE_ECAM_BASE_PHYS,
         PCIE_ECAM_BASE_PHYS + PCIE_ECAM_SIZE);
  zx::vmo ecam_vmo = {};
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  status = zx::vmo::create_physical(*zx::unowned_resource(get_root_resource()), PCIE_ECAM_BASE_PHYS,
                                    PCIE_ECAM_SIZE, &ecam_vmo);
  if (status != ZX_OK) {
    return status;
  }

  ctx.info.ecam_vmo = ecam_vmo.release();
  status = QemuArm64Pciroot::Create(&pci_root_host_, ctx, parent_, name.data());
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

}  // namespace board_qemu_arm64
