// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "acpi.h"

#include <lib/acpi_lite.h>

#include <phys/acpi.h>
#include <phys/symbolize.h>

#include "legacy-boot-shim.h"

void InitAcpi(LegacyBootShim& shim) {
  if (auto acpi_parser_or = MakeAcpiParser(gLegacyBoot.acpi_rsdp); acpi_parser_or.is_ok()) {
    shim.InitAcpi(*acpi_parser_or);
  } else {
    printf("%s: Cannot find ACPI tables (%" PRId32 ") from %#" PRIx64 "\n", ProgramName(),
           acpi_parser_or.error_value(), gLegacyBoot.acpi_rsdp);
  }
}
