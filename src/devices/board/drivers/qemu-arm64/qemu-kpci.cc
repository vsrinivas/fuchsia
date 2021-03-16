// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/syscalls/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

#include "qemu-bus.h"
#include "qemu-virt.h"

namespace board_qemu_arm64 {

zx_status_t QemuArm64::PciInit() {
  zx_status_t status;

  zx_pci_init_arg_t* arg1;
  size_t arg_size = sizeof(*arg1) + sizeof(arg1->addr_windows[0]);  // room for one addr window
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[arg_size]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto* arg = reinterpret_cast<zx_pci_init_arg_t*>(buf.get());

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  status = zx_pci_add_subtract_io_range(get_root_resource(), true /* mmio */, PCIE_MMIO_BASE_PHYS,
                                        PCIE_MMIO_SIZE, true /* add */);
  if (status != ZX_OK) {
    return status;
  }
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  status = zx_pci_add_subtract_io_range(get_root_resource(), false /* pio */, PCIE_PIO_BASE_PHYS,
                                        PCIE_PIO_SIZE, true /* add */);
  if (status != ZX_OK) {
    return status;
  }

  // initialize our swizzle table
  zx_pci_irq_swizzle_lut_t* lut = &arg->dev_pin_to_global_irq;
  for (unsigned dev_id = 0; dev_id < ZX_PCI_MAX_DEVICES_PER_BUS; dev_id++) {
    for (unsigned func_id = 0; func_id < ZX_PCI_MAX_FUNCTIONS_PER_DEVICE; func_id++) {
      for (unsigned pin = 0; pin < ZX_PCI_MAX_LEGACY_IRQ_PINS; pin++) {
        (*lut)[dev_id][func_id][pin] = PCIE_INT_BASE + (pin + dev_id) % ZX_PCI_MAX_LEGACY_IRQ_PINS;
      }
    }
  }
  arg->num_irqs = 0;
  arg->addr_window_count = 1;
  arg->addr_windows[0].cfg_space_type = PCI_CFG_SPACE_TYPE_MMIO;
  arg->addr_windows[0].has_ecam = true;
  arg->addr_windows[0].base = PCIE_ECAM_BASE_PHYS;
  arg->addr_windows[0].size = PCIE_ECAM_SIZE;
  arg->addr_windows[0].bus_start = 0;
  arg->addr_windows[0].bus_end = (PCIE_ECAM_SIZE / ZX_PCI_ECAM_BYTE_PER_BUS) - 1;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  status = zx_pci_init(get_root_resource(), arg, static_cast<uint32_t>(arg_size));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: error %d in zx_pci_init", __func__, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t QemuArm64::PciAdd() {
  constexpr pbus_bti_t kPciBtis[] = {
      {
          .iommu_index = 0,
          .bti_id = 0,
      },
  };

  pbus_dev_t pci_dev = {};
  pci_dev.name = "pci";
  pci_dev.vid = PDEV_VID_GENERIC;
  pci_dev.pid = PDEV_PID_GENERIC;
  pci_dev.did = PDEV_DID_KPCI;
  pci_dev.bti_list = kPciBtis;
  pci_dev.bti_count = countof(kPciBtis);

  auto status = pbus_.DeviceAdd(&pci_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_qemu_arm64
