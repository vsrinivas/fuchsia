// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/phys_mem.h"

#include <lib/zx/vmar.h>

zx_status_t PhysMem::Init(zx::vmo vmo) {
  size_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to query VMO size";
    return status;
  }

  // This PhysMem instantiation is not using a device memory aware VMAR mapping, so create a single
  // guest memory region encompassing all guest physical memory.
  std::vector<GuestMemoryRegion> guest_mem = {{.base = 0, .size = vmo_size}};

  return Init(guest_mem, std::move(vmo));
}

zx_status_t PhysMem::Init(const std::vector<GuestMemoryRegion>& guest_mem, zx::vmo vmo) {
  vmo_ = std::move(vmo);

  const GuestMemoryRegion& final_region =
      *std::max_element(guest_mem.begin(), guest_mem.end(), GuestMemoryRegion::CompareMinByBase);
  vmo_size_ = final_region.base + final_region.size;

  zx_status_t status = zx::vmar::root_self()->allocate(
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0, vmo_size_, &child_vmar_,
      &addr_);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create child VMAR of size " << vmo_size_;
    return status;
  }

  static const uint32_t page_size = zx_system_get_page_size();
  for (const GuestMemoryRegion& region : guest_mem) {
    FX_CHECK(region.base % page_size == 0)
        << std::hex
        << "Guest memory region must start at a page aligned address, but region begins at 0x"
        << region.base;
    FX_CHECK(region.size % page_size == 0)
        << std::hex
        << "Guest memory region must end at a page aligned address, but region ends at 0x"
        << (region.base + region.size);

    zx_gpaddr_t addr_within_child;
    status = child_vmar_.map(
        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC | ZX_VM_REQUIRE_NON_RESIZABLE,
        region.base, vmo_, region.base, region.size, &addr_within_child);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to map guest physical memory region " << region.base
                              << " - " << region.base + region.size;
      return status;
    }
  }

  return ZX_OK;
}

PhysMem::~PhysMem() {
  if (child_vmar_.is_valid()) {
    zx_status_t status = child_vmar_.destroy();
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to destroy child VMAR";
    }
  }
}
