// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_ZIRCON_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_ZIRCON_H_

#include <lib/acpi_lite.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

namespace acpi_lite {

// Convert physical addresses to virtual addresses using Zircon's standard conversion
// functions.
class ZirconPhysmemReader final : public PhysMemReader {
 public:
  constexpr ZirconPhysmemReader() = default;

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) final;
};

// Create a new AcpiParser, starting at the given Root System Description Pointer (RSDP),
// and using Zircon's |paddr_to_physmap| implementation to convert physical addresses
// to virtual addresses.
zx::status<AcpiParser> AcpiParserInit(zx_paddr_t rsdp_pa);

}  // namespace acpi_lite

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_INCLUDE_LIB_ACPI_LITE_ZIRCON_H_
