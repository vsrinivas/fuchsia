// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "acpi.h"

#include <lib/acpi_lite.h>

#include <phys/symbolize.h>

#include "legacy-boot-shim.h"

void InitAcpi(LegacyBootShim& shim) {
  class PhysPhysMemReader final : public acpi_lite::PhysMemReader {
   public:
    constexpr PhysPhysMemReader() = default;

    zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) final {
      return zx::success(reinterpret_cast<const void*>(phys));
    }
  };

  // If the RSDP address is 0, AcpiParser::Init will try to find it by magic.
  uint64_t rsdp = gLegacyBoot.acpi_rsdp;

  if (static_cast<uintptr_t>(rsdp) != rsdp) {
    printf("%s: ACPI tables (%#" PRIx64 ") were reportedly not found within the lower 4GiB\n",
           Symbolize::kProgramName_, rsdp);
    return;
  }

  PhysPhysMemReader mem_reader;
  auto acpi_parser = acpi_lite::AcpiParser::Init(mem_reader, static_cast<zx_paddr_t>(rsdp));
  if (acpi_parser.is_ok()) {
    shim.InitAcpi(acpi_parser.value());
  } else {
    printf("%s: Cannot find ACPI tables (%" PRId32 ") from %#" PRIx64 "\n",
           Symbolize::kProgramName_, acpi_parser.error_value(), rsdp);
  }
}
