// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/acpi-memory-region.h"

#include <lib/ddk/driver.h>
#include <lib/stdcompat/span.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/resource.h>
#include <lib/zx/result.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstdint>
#include <limits>

#include "src/graphics/display/drivers/intel-i915-tgl/acpi-memory-region-util.h"

namespace i915_tgl {

// static
zx::result<AcpiMemoryRegion> AcpiMemoryRegion::Create(zx_paddr_t region_base, size_t region_size) {
  auto [first_page_physical_address, vmo_size] = RoundToPageBoundaries(region_base, region_size);

  // The static_cast below is lossless because of this.
  static_assert(PAGE_SIZE < std::numeric_limits<uint32_t>::max());

  // The offset of the region's start, within the region's first page.
  const uint32_t page_offset = static_cast<uint32_t>(region_base ^ first_page_physical_address);

  // TODO(fxbug.dev/31358): We use `get_root_resource()` here because we need to
  // map some memory whose physical address is only known at runtime.
  //
  // The IGD OpRegion specification asks the boot firmware to place the memory
  // regions we're interested in (Memory OpRegion, extended Video BIOS Table) in
  // one ACPI custom Operation Region of Type 4 (NVS = Non-Volatile Sleeping
  // Memory). So, this entire method should be replaced by an ACPI driver call
  // that returns a VMO representing the ACPI custom Operation Region that
  // contains a given physical address.
  zx::vmo region_vmo;
  zx_status_t status = zx::vmo::create_physical(*zx::unowned_resource(get_root_resource()),
                                                first_page_physical_address, vmo_size, &region_vmo);
  if (status != ZX_OK) {
    return zx::error_result(status);
  }

  zx_vaddr_t first_page_address;
  status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, /*vmar_offset=*/0,
                                      region_vmo, /*vmo_offset=*/0, vmo_size, &first_page_address);
  if (status != ZX_OK) {
    return zx::error_result(status);
  }

  const zx_vaddr_t region_address = static_cast<zx_vaddr_t>(first_page_address + page_offset);
  const cpp20::span<uint8_t> region_data(reinterpret_cast<uint8_t*>(region_address), region_size);
  return zx::ok(AcpiMemoryRegion(std::move(region_vmo), region_data));
}

AcpiMemoryRegion::AcpiMemoryRegion(zx::vmo region_vmo, cpp20::span<uint8_t> region_data)
    : region_data_(region_data), region_vmo_(std::move(region_vmo)) {
  ZX_ASSERT(!region_data.empty());
}

AcpiMemoryRegion::AcpiMemoryRegion(AcpiMemoryRegion&& rhs) noexcept
    : region_data_(rhs.region_data_), region_vmo_(std::move(rhs.region_vmo_)) {
  rhs.region_data_ = cpp20::span<uint8_t>();
}

AcpiMemoryRegion& AcpiMemoryRegion::operator=(AcpiMemoryRegion&& rhs) noexcept {
  // We use the swapping approach because the best alternative seems
  // non-trivial. The alternative is to destroy the state in `lhs` and turn
  // `rhs` into an empty region. Destroying the state in `lhs` comes down to a
  // conditional VMAR unmapping, and closing the underlying the VMO.

  std::swap(region_vmo_, rhs.region_vmo_);
  std::swap(region_data_, rhs.region_data_);
  return *this;
}

AcpiMemoryRegion::~AcpiMemoryRegion() {
  if (region_vmo_.is_valid()) {
    const zx_vaddr_t region_base = reinterpret_cast<zx_vaddr_t>(region_data_.data());
    auto [first_page_address, vmo_size] = RoundToPageBoundaries(region_base, region_data_.size());
    zx::vmar::root_self()->unmap(region_base, vmo_size);
  }
}

}  // namespace i915_tgl
