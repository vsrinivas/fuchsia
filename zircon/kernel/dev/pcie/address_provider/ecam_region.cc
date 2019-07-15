// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/address_provider/ecam_region.h>
#include <dev/pci_common.h>
#include <vm/vm_aspace.h>

MappedEcamRegion::~MappedEcamRegion() {
  if (vaddr_ != nullptr) {
    VmAspace::kernel_aspace()->FreeRegion(reinterpret_cast<vaddr_t>(vaddr_));
  }
}

zx_status_t MappedEcamRegion::MapEcam() {
  DEBUG_ASSERT(ecam_.bus_start <= ecam_.bus_end);

  // TODO(gkalsi): These asserts are helpful but they don't apply for the DWC
  //               since the ECAM is broken up and mapped in different places.
  //               We should find a way to enforce these only for MMIO ECAMs
  // DEBUG_ASSERT((ecam_.size % PCIE_ECAM_BYTE_PER_BUS) == 0);
  // DEBUG_ASSERT((ecam_.size / PCIE_ECAM_BYTE_PER_BUS) ==
  // (static_cast<size_t>(ecam_.bus_end) - ecam_.bus_start + 1u));

  if (vaddr_ != nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  char name_buf[32];
  snprintf(name_buf, sizeof(name_buf), "pcie_cfg_%02x_%02x", ecam_.bus_start, ecam_.bus_end);

  return VmAspace::kernel_aspace()->AllocPhysical(
      name_buf, ecam_.size, &vaddr_, PAGE_SIZE_SHIFT, ecam_.phys_base, 0 /* vmm flags */,
      ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
}
