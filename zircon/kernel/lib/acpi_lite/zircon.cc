// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/acpi_lite.h>
#include <lib/acpi_lite/zircon.h>
#include <zircon/compiler.h>

#include <vm/physmap.h>

namespace acpi_lite {

zx::status<const void*> ZirconPhysmemReader::PhysToPtr(uintptr_t phys, size_t length) {
  // We don't support the 0 physical address or 0-length ranges.
  if (length == 0 || phys == 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  // Get the last byte of the specified range, ensuring we don't wrap around the address
  // space.
  uintptr_t phys_end;
  if (add_overflow(phys, length - 1, &phys_end)) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  // Ensure that both "phys" and "phys + length - 1" have valid addresses.
  //
  // The Zircon physmap is contiguous, so we don't have to worry about intermediate addresses.
  if (!is_physmap_phys_addr(phys) || !is_physmap_phys_addr(phys_end)) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  return zx::success(paddr_to_physmap(phys));
}

// Create a new AcpiParser, starting at the given Root System Description Pointer (RSDP).
zx::status<AcpiParser> AcpiParserInit(zx_paddr_t rsdp_pa) {
  // AcpiParser requires a ZirconPhysmemReader instance that outlives
  // it. We share a single static instance for all AcpiParser instances.
  static ZirconPhysmemReader reader;

  return AcpiParser::Init(reader, rsdp_pa);
}

}  // namespace acpi_lite
