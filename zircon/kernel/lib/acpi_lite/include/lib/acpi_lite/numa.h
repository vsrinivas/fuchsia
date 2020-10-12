// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_NUMA_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_NUMA_H_

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/structures.h>
#include <zircon/types.h>

#include <fbl/function.h>

namespace acpi_lite {

constexpr uint8_t kAcpiMaxNumaRegions = 5;

// A region of memory associated with a NUMA domain.
struct AcpiNumaRegion {
  uint64_t base_address;
  uint64_t length;
};

// A NUMA domain.
struct AcpiNumaDomain {
  uint32_t domain;
  AcpiNumaRegion memory[kAcpiMaxNumaRegions];
  uint8_t memory_count;
};

// Calls the given callback on all pairs of CPU APIC ID and NumaRegion.
zx_status_t EnumerateCpuNumaPairs(
    const AcpiSratTable* srat,
    const fbl::Function<void(const AcpiNumaDomain&, uint32_t)>& callback);
zx_status_t EnumerateCpuNumaPairs(const AcpiParserInterface& parser,
                                  fbl::Function<void(const AcpiNumaDomain&, uint32_t)> callback);

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_NUMA_H_
